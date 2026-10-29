#include "TriggerHaptics.h"

#include "Log.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace tf2vr
{
namespace
{
struct ModeInfo
{
    const char* name;
    uint8_t mode;
    int parameters;
};

// mode numbers and parameter order of ScePadTriggerEffectMode and its parameter structs
constexpr ModeInfo kModes[] = {
    {"off", 0, 0},   {"feedback", 1, 2}, {"weapon", 2, 3},          {"vibration", 3, 3},
    {"multi", 4, 10}, {"slope", 5, 4},    {"multi_vibration", 6, 11},
};

const ModeInfo* FindMode(uint8_t mode)
{
    for (const ModeInfo& info : kModes)
        if (info.mode == mode)
            return &info;
    return nullptr;
}

// the layout PSVR2Toolkit expects for ScePadTriggerEffectCommand
struct TriggerCommand
{
    int32_t mode;
    uint8_t padding[4];
    uint8_t data[48];
};
static_assert(sizeof(TriggerCommand) == 56);

constexpr int kResultOk = 0;
constexpr int kResultDriverInactive = -1;
constexpr const wchar_t* kCapiDll = L"psvr2_toolkit_capi.dll";
constexpr const wchar_t* kCapiSubPath = L"\\steamapps\\common\\PlayStation VR2 App\\SteamVR_Plug-In\\bin\\win64\\psvr2_toolkit_capi.dll";

using InitFn = int (*)();
using SetTriggerEffectFn = int (*)(uint8_t controllerType, const TriggerCommand& command);

struct Shared
{
    std::mutex mutex;
    std::condition_variable wake;
    TriggerEffect desired[2];
    bool started = false;
};

Shared& State()
{
    // never destroyed: the worker thread may still run while the process exits
    static Shared* shared = new Shared();
    return *shared;
}

bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

// Steam itself plus every library in steamapps\libraryfolders.vdf
std::vector<std::wstring> SteamLibraries()
{
    std::vector<std::wstring> libraries;
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return libraries;
    std::wstring steam = buffer;
    libraries.push_back(steam);

    std::ifstream vdf(steam + L"\\steamapps\\libraryfolders.vdf");
    std::string line;
    while (std::getline(vdf, line))
    {
        size_t key = line.find("\"path\"");
        if (key == std::string::npos)
            continue;
        size_t open = line.find('"', key + 6);
        size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close == std::string::npos)
            continue;
        std::string path;
        std::string quoted = line.substr(open + 1, close - open - 1);
        for (size_t i = 0; i < quoted.size(); ++i)
        {
            if (quoted[i] == '\\' && i + 1 < quoted.size() && quoted[i + 1] == '\\')
                ++i;
            path += quoted[i];
        }
        libraries.push_back(Widen(path));
    }
    return libraries;
}

std::wstring FindCapi(const std::wstring& override)
{
    if (!override.empty())
        return override;
    for (const std::wstring& library : SteamLibraries())
    {
        std::wstring candidate = library + kCapiSubPath;
        if (FileExists(candidate))
            return candidate;
    }
    return {};
}

void Worker(std::wstring capiPathOverride)
{
    std::wstring path = FindCapi(capiPathOverride);
    if (path.empty())
    {
        Log("triggers: PSVR2Toolkit not found (no %ls in the PlayStation VR2 App), adaptive triggers off", kCapiDll);
        return;
    }
    // the toolkit's libcrossipc.dll sits next to it
    HMODULE capi = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto init = capi ? reinterpret_cast<InitFn>(GetProcAddress(capi, "psvr2_toolkit_init")) : nullptr;
    auto setTrigger = capi ? reinterpret_cast<SetTriggerEffectFn>(GetProcAddress(capi, "psvr2_toolkit_set_trigger_effect")) : nullptr;
    if (!init || !setTrigger)
    {
        Log("triggers: could not load %ls (error %lu), adaptive triggers off", path.c_str(), capi ? 0ul : GetLastError());
        return;
    }
    Log("triggers: loaded %ls", path.c_str());

    Shared& shared = State();
    bool initialized = false;
    int lastResult[2] = {kResultOk, kResultOk};
    int lastInit = kResultOk;
    // a new toolkit slot starts with both triggers off
    TriggerEffect applied[2];
    for (;;)
    {
        if (!initialized)
        {
            int result = init();
            if (result != kResultOk)
            {
                if (result != lastInit)
                    Log("triggers: PSVR2Toolkit not ready (%d), is SteamVR running with the PS VR2? retrying", result);
                lastInit = result;
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }
            Log("triggers: connected to PSVR2Toolkit");
            initialized = true;
        }

        TriggerEffect desired[2];
        {
            std::unique_lock lock(shared.mutex);
            shared.wake.wait_for(lock, std::chrono::seconds(2), [&] { return shared.desired[0] != applied[0] || shared.desired[1] != applied[1]; });
            desired[0] = shared.desired[0];
            desired[1] = shared.desired[1];
        }

        bool failed = false;
        for (int hand = 0; hand < 2; ++hand)
        {
            if (desired[hand] == applied[hand])
                continue;
            TriggerCommand command = {};
            command.mode = desired[hand].mode;
            std::memcpy(command.data, desired[hand].data, sizeof(desired[hand].data));
            int result = setTrigger(static_cast<uint8_t>(hand), command);
            if (result == kResultOk)
            {
                applied[hand] = desired[hand];
                Log("triggers: %s trigger %s", hand == 0 ? "left" : "right", DescribeTriggerEffect(desired[hand]).c_str());
            }
            else
            {
                if (result != lastResult[hand])
                    Log("triggers: setting the %s trigger failed (%d)", hand == 0 ? "left" : "right", result);
                failed = true;
                // SteamVR restarted: the driver forgot our effects, send them again once it is back
                if (result == kResultDriverInactive)
                    applied[0] = applied[1] = TriggerEffect{0xff};
            }
            lastResult[hand] = result;
        }
        if (failed)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
}

bool TriggerEffect::operator==(const TriggerEffect& other) const
{
    return mode == other.mode && std::memcmp(data, other.data, sizeof(data)) == 0;
}

bool ParseTriggerEffect(const std::string& text, TriggerEffect& effect)
{
    std::istringstream stream(text);
    std::string name;
    if (!(stream >> name))
        return false;
    for (const ModeInfo& info : kModes)
    {
        if (name != info.name)
            continue;
        TriggerEffect parsed;
        parsed.mode = info.mode;
        for (int i = 0; i < info.parameters; ++i)
        {
            int value = 0;
            if (!(stream >> value) || value < 0 || value > 255)
                return false;
            parsed.data[i] = static_cast<uint8_t>(value);
        }
        std::string extra;
        if (stream >> extra)
            return false;
        effect = parsed;
        return true;
    }
    return false;
}

std::string DescribeTriggerEffect(const TriggerEffect& effect)
{
    const ModeInfo* info = FindMode(effect.mode);
    if (!info)
        return "unknown";
    std::string text = info->name;
    for (int i = 0; i < info->parameters; ++i)
        text += " " + std::to_string(effect.data[i]);
    return text;
}

namespace triggers
{
void Start(const std::wstring& capiPathOverride)
{
    Shared& shared = State();
    {
        std::lock_guard lock(shared.mutex);
        if (shared.started)
            return;
        shared.started = true;
    }
    std::thread(Worker, capiPathOverride).detach();
}

void SetTrigger(int hand, const TriggerEffect& effect)
{
    if (hand < 0 || hand > 1)
        return;
    Shared& shared = State();
    std::lock_guard lock(shared.mutex);
    if (!shared.started || shared.desired[hand] == effect)
        return;
    shared.desired[hand] = effect;
    shared.wake.notify_one();
}
}
}
