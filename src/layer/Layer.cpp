#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <optional>
#include <share.h>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr float kHeld = 0.5f;
constexpr auto kGunGrabWindow = 500ms;
constexpr auto kReloadEndGrace = 200ms;
constexpr auto kGrenadeMinPress = 150ms;
constexpr uint64_t kSprintStopGapFrames = 2;
constexpr uint64_t kSprintStopFrames = 6;
constexpr uint64_t kJumpTapFrames = 6;

#ifdef TF2VRBB_LOGGING
std::mutex g_logMutex;
FILE* g_log = nullptr;

void LogOpen(const std::wstring& path)
{
    g_log = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
}

void Log(const char* format, ...)
{
    std::lock_guard lock(g_logMutex);
    if (!g_log)
        return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    std::fprintf(g_log, "[%02u:%02u:%02u.%03u] [t%lu] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());
    va_list args;
    va_start(args, format);
    std::vfprintf(g_log, format, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}
#else
void LogOpen(const std::wstring&)
{
}

template <typename... Args>
void Log(const char*, const Args&...)
{
}
#endif

struct Config
{
    bool reloadGrab;
    bool grenadeToggle;
    bool crouchButtonMode;
    bool uncrouchOnJump;
    int jumpDelayMs;
    bool blockTrigger;
    bool sprint;
    int sprintStartCm;
    int sprintStopCm;
};

Config LoadConfig(const std::wstring& path)
{
    auto number = [&](const wchar_t* section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
    };
    wchar_t mode[32] = {};
    GetPrivateProfileStringW(L"crouch", L"mode", L"toggle", mode, 32, path.c_str());
    Config config;
    config.reloadGrab = number(L"reload_grab", L"enabled", 1) != 0;
    config.grenadeToggle = number(L"grenade", L"toggle", 0) != 0;
    config.crouchButtonMode = std::wcscmp(mode, L"button") == 0;
    config.uncrouchOnJump = number(L"crouch", L"uncrouch_on_jump", 0) != 0;
    config.jumpDelayMs = std::max(number(L"crouch", L"jump_delay_ms", 250), 0);
    config.blockTrigger = number(L"tactical", L"block_hand_trigger_in_game", 1) != 0;
    config.sprint = number(L"sprint", L"enabled", 0) != 0;
    config.sprintStartCm = number(L"sprint", L"start_below_eyes_cm", 30);
    config.sprintStopCm = number(L"sprint", L"stop_below_eyes_cm", 20);
    return config;
}

void* FindConVar(const char* name)
{
    __try
    {
        HMODULE vstdlib = GetModuleHandleW(L"vstdlib.dll");
        auto createInterface = vstdlib ? reinterpret_cast<void* (*)(const char*, int*)>(GetProcAddress(vstdlib, "CreateInterface")) : nullptr;
        void* cvar = createInterface ? createInterface("VEngineCvar007", nullptr) : nullptr;
        if (!cvar)
            return nullptr;
        auto findVar = reinterpret_cast<void* (*)(void*, const char*)>((*static_cast<void***>(cvar))[16]);
        auto conVar = static_cast<unsigned char*>(findVar(cvar, name));
        const char* actual = conVar ? *reinterpret_cast<const char* const*>(conVar + 0x18) : nullptr;
        return actual && std::strcmp(actual, name) == 0 ? conVar : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

struct GameVar
{
    const char* name;
    unsigned char* conVar = nullptr;
    ULONGLONG retryAt = 0;

    std::optional<int> Read()
    {
        if (!conVar)
        {
            ULONGLONG now = GetTickCount64();
            if (now < retryAt)
                return std::nullopt;
            retryAt = now + 1000;
            conVar = static_cast<unsigned char*>(FindConVar(name));
            if (!conVar)
                return std::nullopt;
            Log("game var %s found", name);
        }
        return *reinterpret_cast<const volatile int*>(conVar + 0x5C);
    }
};

using CbufAddTextFn = void (*)(int target, const char* text, int source);

CbufAddTextFn FindCbufAddText()
{
    constexpr uintptr_t rva = 0x1203B0;
    constexpr unsigned char prologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
                                          0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xD9, 0x48, 0x8D, 0x0D};
    auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(L"engine.dll"));
    if (!base)
    {
        Log("engine.dll is not loaded");
        return nullptr;
    }
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < rva + sizeof(prologue) || std::memcmp(base + rva, prologue, sizeof(prologue)) != 0)
    {
        Log("engine: Cbuf_AddText not found, console commands disabled");
        return nullptr;
    }
    return reinterpret_cast<CbufAddTextFn>(const_cast<unsigned char*>(base + rva));
}

bool GameCommand(const char* command)
{
    static const CbufAddTextFn addText = FindCbufAddText();
    if (!addText)
        return false;
    addText(0, (std::string(command) + "\n").c_str(), 0);
    return true;
}

template <typename State, typename Value>
void Override(State* state, Value value, bool changed, XrTime changeTime)
{
    state->isActive = XR_TRUE;
    state->currentState = value;
    state->changedSinceLastSync = changed;
    if (changed && changeTime != 0)
        state->lastChangeTime = changeTime;
}

bool NewPositive(std::optional<int>& last, std::optional<int> value)
{
    if (!value)
        return false;
    bool fresh = last && *value != *last && *value > 0;
    last = value;
    return fresh;
}

struct Next
{
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrStringToPath stringToPath = nullptr;
    PFN_xrCreateActionSet createActionSet = nullptr;
    PFN_xrCreateAction createAction = nullptr;
    PFN_xrSyncActions syncActions = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f = nullptr;
    PFN_xrCreateActionSpace createActionSpace = nullptr;
    PFN_xrLocateSpace locateSpace = nullptr;
    PFN_xrLocateSpaces locateSpaces = nullptr;
    PFN_xrLocateViews locateViews = nullptr;
};

struct Grip
{
    XrPath path = XR_NULL_PATH;
    const char* name = "";
    float output = 0.0f;
    bool changed = false;
    XrTime changeTime = 0;
    bool sampled = false;
    bool routed = false;
    bool grabBound = false;
    bool grabPressed = false;
    bool grabLatched = false;
    bool rawPressed = false;
    Clock::time_point pressAt;
    bool held = false;
    Clock::time_point heldAt;
    bool grenadeHeld = false;
    bool waitRelease = false;
};

struct Pose
{
    bool valid = false;
    float height = 0.0f;
    XrSpace base = XR_NULL_HANDLE;
    XrTime time = 0;
};

struct Button
{
    bool active = false;
    bool pressed = false;
    bool changed = false;
    XrTime changeTime = 0;
};

struct Layer
{
    Next next;
    XrInstance instance = XR_NULL_HANDLE;
    XrPath left = XR_NULL_PATH;
    XrPath right = XR_NULL_PATH;
    XrPath gunHand = XR_NULL_PATH;
    XrPath tacticalHand = XR_NULL_PATH;
    XrAction squeeze = XR_NULL_HANDLE;
    XrAction trigger = XR_NULL_HANDLE;
    XrAction aim = XR_NULL_HANDLE;
    XrAction stick = XR_NULL_HANDLE;
    XrAction stickClick = XR_NULL_HANDLE;
    XrAction jump = XR_NULL_HANDLE;
    XrAction reloadGrab = XR_NULL_HANDLE;
    XrAction crouch = XR_NULL_HANDLE;
    XrAction tactical = XR_NULL_HANDLE;
    std::vector<Grip> grips;
    std::vector<std::pair<XrSpace, XrPath>> aimSpaces;
    Pose gun;
    Pose head;
    uint64_t frame = 0;

    GameVar menuVar{"tf2vrbb_menu_active"};
    GameVar highlightVar{"tf2vr_reload_highlight"};
    GameVar gunAckVar{"tf2vr_gun_ack"};
    GameVar grenadeAckVar{"tf2vr_grenade_ack"};
    GameVar gunGripToggleVar{"tf2vr_toggle_grip"};
    GameVar titanVar{"tf2vrbb_titan"};
    int menu = -2;
    bool titan = false;
    int highlight = 0;
    bool reloadWaiting = false;
    Clock::time_point reloadSeenAt;
    std::optional<int> gunAck;
    std::optional<int> grenadeAck;
    bool grenadeToggle = false;

    Button crouchButton;
    bool jumpPressed = false;
    bool crouchInverted = false;
    bool crouchReleased = false;
    bool ducked = false;
    bool jumpPending = false;
    Clock::time_point jumpReleaseAt;
    uint64_t jumpTapEnd = 0;
    bool jumpOverride = false;
    bool jumpOutput = false;
    bool jumpChanged = false;

    Button tacticalButton;
    bool triggerApplies = false;
    bool triggerBlocked = false;
    bool triggerReloadBlocked = false;
    bool triggerWaitRelease = false;
    float triggerOutput = 0.0f;
    bool triggerChanged = false;
    XrTime triggerChangeTime = 0;

    bool lowered = false;
    bool loggedSpaceMismatch = false;
    uint64_t stopStart = 0;
    uint64_t stopEnd = 0;
    bool sprintPressed = false;
    bool stickZeroed = false;
    bool stickChanged = false;
    bool sprintOutput = false;
    bool sprintChanged = false;
    XrTime sprintChangeTime = 0;

    bool Resolve()
    {
        auto get = [this](const char* name, auto& function) {
            PFN_xrVoidFunction pointer = nullptr;
            if (XR_FAILED(next.getInstanceProcAddr(instance, name, &pointer)) || !pointer)
            {
                Log("next layer is missing %s", name);
                return false;
            }
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(pointer);
            return true;
        };
        bool resolved = get("xrStringToPath", next.stringToPath) && get("xrCreateActionSet", next.createActionSet) &&
                        get("xrCreateAction", next.createAction) && get("xrSyncActions", next.syncActions) &&
                        get("xrGetActionStateFloat", next.getActionStateFloat) && get("xrGetActionStateBoolean", next.getActionStateBoolean) &&
                        get("xrGetActionStateVector2f", next.getActionStateVector2f) && get("xrCreateActionSpace", next.createActionSpace) &&
                        get("xrLocateSpace", next.locateSpace) && get("xrLocateViews", next.locateViews);
        PFN_xrVoidFunction locateSpaces = nullptr;
        if (resolved && XR_SUCCEEDED(next.getInstanceProcAddr(instance, "xrLocateSpaces", &locateSpaces)))
            next.locateSpaces = reinterpret_cast<PFN_xrLocateSpaces>(locateSpaces);
        return resolved;
    }

    XrPath ToPath(const char* text)
    {
        XrPath path = XR_NULL_PATH;
        next.stringToPath(instance, text, &path);
        return path;
    }

    const char* HandName(XrPath hand) const
    {
        return hand == left ? "left" : hand == right ? "right" : "unknown";
    }

    XrAction CreateAction(XrActionSet set, const char* name, const char* localizedName, bool perHand)
    {
        XrPath hands[] = {left, right};
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        strcpy_s(info.actionName, name);
        strcpy_s(info.localizedActionName, localizedName);
        info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        info.countSubactionPaths = perHand ? 2 : 0;
        info.subactionPaths = perHand ? hands : nullptr;
        XrAction action = XR_NULL_HANDLE;
        XrResult result = next.createAction(set, &info, &action);
        Log("action '%s' created (%d)", name, result);
        return XR_SUCCEEDED(result) ? action : XR_NULL_HANDLE;
    }

    bool ReadBoolean(XrSession session, XrAction action, XrPath hand, XrActionStateBoolean& state)
    {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = action;
        get.subactionPath = hand;
        return XR_SUCCEEDED(next.getActionStateBoolean(session, &get, &state));
    }

    bool ReadFloat(XrSession session, XrAction action, XrPath hand, XrActionStateFloat& state)
    {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = action;
        get.subactionPath = hand;
        return XR_SUCCEEDED(next.getActionStateFloat(session, &get, &state)) && state.isActive;
    }

    bool UpdateButton(XrSession session, XrAction action, Button& button, const char* label)
    {
        button.changed = false;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (action == XR_NULL_HANDLE || !ReadBoolean(session, action, XR_NULL_PATH, state))
            return false;
        button.active = state.isActive;
        bool pressed = state.isActive && state.currentState;
        if (pressed != button.pressed)
        {
            button.pressed = pressed;
            button.changed = true;
            button.changeTime = state.lastChangeTime;
            Log("%s %s", label, pressed ? "pressed" : "released");
        }
        return true;
    }

    Grip* FindGrip(XrPath path)
    {
        auto it = std::find_if(grips.begin(), grips.end(), [path](const Grip& grip) { return grip.path == path; });
        return it == grips.end() ? nullptr : &*it;
    }

    Grip* RecentGrab()
    {
        Grip* newest = nullptr;
        for (Grip& grip : grips)
            if (Clock::now() - grip.heldAt <= kGunGrabWindow && (!newest || grip.heldAt > newest->heldAt))
                newest = &grip;
        return newest;
    }

    void UpdateMenu()
    {
        int value = menuVar.Read().value_or(-1);
        if (value != menu)
            Log("menu state: %s", value < 0 ? "unknown (companion script not running)" : value ? "menu or start screen" : "gameplay");
        menu = value;
        bool inTitan = titanVar.Read().value_or(0) == 1;
        if (inTitan != titan)
            Log("titan %s", inTitan ? "entered, trigger left alone" : "left");
        titan = inTitan;
    }

    void UpdateReload()
    {
        int value = highlightVar.Read().value_or(0);
        if (value != highlight)
            Log("reload highlight %d", value);
        highlight = value;
        bool latched = std::any_of(grips.begin(), grips.end(), [](const Grip& grip) { return grip.grabLatched; });
        if (highlight != 0 || latched)
            reloadSeenAt = Clock::now();
        bool waiting = highlight != 0 || latched || (reloadWaiting && Clock::now() - reloadSeenAt < kReloadEndGrace);
        if (waiting != reloadWaiting)
            Log("reload %s", waiting ? "started" : "finished");
        reloadWaiting = waiting;
    }

    void UpdateSqueeze(XrSession session)
    {
        for (Grip& grip : grips)
        {
            XrActionStateFloat squeezeState{XR_TYPE_ACTION_STATE_FLOAT};
            float raw = ReadFloat(session, squeeze, grip.path, squeezeState) ? squeezeState.currentState : 0.0f;
            XrActionStateBoolean grab{XR_TYPE_ACTION_STATE_BOOLEAN};
            grip.grabBound = reloadGrab != XR_NULL_HANDLE && ReadBoolean(session, reloadGrab, grip.path, grab) && grab.isActive;
            bool grabbing = grip.grabBound && grab.currentState;

            auto now = Clock::now();
            bool rawPressed = raw > kHeld;
            bool rawEdge = rawPressed && !grip.rawPressed;
            if (rawEdge)
                grip.pressAt = now;
            if (rawPressed != grip.rawPressed)
                Log("%s hand: grip %s", grip.name, rawPressed ? "pressed" : "released");
            grip.rawPressed = rawPressed;
            if (grabbing != grip.grabPressed)
                Log("%s hand: reload grab %s", grip.name, grabbing ? "pressed" : "released");
            grip.grabPressed = grabbing;

            bool routed = grip.grabBound && reloadWaiting && grip.path != gunHand && !grip.grenadeHeld;
            if (routed != grip.routed)
                Log("%s hand: %s", grip.name, routed ? "grip blocked, reload grab active" : "grip restored");
            grip.routed = routed;
            bool latched = routed && grabbing;
            if (latched != grip.grabLatched)
                Log("%s hand: %s", grip.name, latched ? "reload grab holds" : "reload grab let go");
            grip.grabLatched = latched;

            float output = raw;
            if (grip.grenadeHeld)
            {
                output = rawEdge ? 0.0f : 1.0f;
                if (rawEdge)
                {
                    grip.grenadeHeld = false;
                    grip.waitRelease = true;
                    Log("%s hand: grenade let go", grip.name);
                }
            }
            else if (grip.waitRelease)
            {
                output = 0.0f;
                grip.waitRelease = rawPressed;
            }
            else if (routed)
                output = grabbing ? 1.0f : 0.0f;
            else if (grenadeToggle && !rawPressed && grip.output > kHeld && now - grip.pressAt < kGrenadeMinPress)
                output = grip.output;

            grip.changed = output != grip.output;
            grip.sampled = routed || grip.changed || output != raw;
            if (grip.changed)
                grip.changeTime = routed ? grab.lastChangeTime : squeezeState.lastChangeTime;
            grip.output = output;
            bool held = output > kHeld;
            if (held && !grip.held)
                grip.heldAt = now;
            grip.held = held;
        }
    }

    void UpdateGunHand()
    {
        if (!NewPositive(gunAck, gunAckVar.Read()))
            return;
        Grip* newest = RecentGrab();
        if (!newest)
        {
            Log("gun ack %d: no recent grab, gun hand stays %s", *gunAck, HandName(gunHand));
            return;
        }
        gunHand = newest->path;
        Log("gun ack %d: gun hand %s", *gunAck, newest->name);
    }

    void UpdateGrenade(bool enabled)
    {
        bool active = enabled && gunGripToggleVar.Read().value_or(0) == 1;
        if (active != grenadeToggle)
            Log("grenade toggle %s", active ? "on (Gun Grip is Toggle)" : "off");
        grenadeToggle = active;
        if (!active || menu != 0)
        {
            for (Grip& grip : grips)
            {
                if (grip.grenadeHeld)
                    Log("%s hand: grenade toggle cleared", grip.name);
                grip.grenadeHeld = false;
            }
        }

        if (!NewPositive(grenadeAck, grenadeAckVar.Read()) || !active)
            return;
        Grip* grip = RecentGrab();
        if (!grip)
            Log("grenade ack %d: no recent grab", *grenadeAck);
        else if (grip->output <= kHeld)
            Log("grenade ack %d: %s hand already let go", *grenadeAck, grip->name);
        else
        {
            grip->grenadeHeld = true;
            Log("grenade ack %d: %s hand holds the grenade until the next grip press", *grenadeAck, grip->name);
        }
    }

    void UpdateCrouch(XrSession session, const Config& config, std::vector<const char*>& commands)
    {
        if (!UpdateButton(session, crouch, crouchButton, "crouch"))
            return;
        bool pressed = crouchButton.pressed;

        bool jumpNow = false;
        bool jumpEdge = false;
        if (jump != XR_NULL_HANDLE)
        {
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            jumpNow = ReadBoolean(session, jump, right, state) && state.isActive && state.currentState;
            jumpEdge = jumpNow && !jumpPressed;
            jumpPressed = jumpNow;
        }

        if (config.crouchButtonMode && !pressed)
            crouchReleased = false;
        bool duck = config.crouchButtonMode ? pressed && !crouchReleased : pressed != crouchInverted;
        if (jumpEdge && duck && config.uncrouchOnJump)
        {
            if (config.crouchButtonMode)
                crouchReleased = true;
            else
                crouchInverted = pressed;
            duck = false;
            jumpPending = true;
            jumpReleaseAt = Clock::now() + std::chrono::milliseconds(config.jumpDelayMs);
            Log("crouch released by jump, jump held back %d ms", config.jumpDelayMs);
        }
        if (jumpPending && Clock::now() >= jumpReleaseAt)
        {
            jumpPending = false;
            if (!jumpNow)
                jumpTapEnd = frame + kJumpTapFrames;
            Log("jump passed on%s", jumpNow ? "" : " as a tap");
        }
        bool tapping = frame < jumpTapEnd;
        bool output = !jumpPending && (jumpNow || tapping);
        jumpChanged = output != jumpOutput;
        jumpOutput = output;
        jumpOverride = jumpPending || tapping;
        if (duck != ducked)
            commands.push_back(duck ? "+duck" : "-duck");
        ducked = duck;
    }

    void UpdateTacticalHand()
    {
        XrPath support = gunHand == left ? right : left;
        if (support == tacticalHand)
            return;
        tacticalHand = support;
        triggerApplies = false;
        triggerBlocked = false;
        Log("tactical hand %s (support hand)", HandName(support));
    }

    void UpdateTrigger(XrSession session, const Config& config)
    {
        triggerChanged = false;
        if (trigger == XR_NULL_HANDLE || titan)
        {
            triggerApplies = false;
            return;
        }
        XrActionStateFloat raw{XR_TYPE_ACTION_STATE_FLOAT};
        bool rawActive = ReadFloat(session, trigger, tacticalHand, raw);
        bool rawPressed = rawActive && raw.currentState > kHeld;

        const Grip* support = FindGrip(tacticalHand);
        bool reloadBlock = menu == 0 && reloadWaiting && support && support->grabBound;
        if (triggerReloadBlocked && !reloadBlock && !reloadWaiting && rawPressed && !triggerWaitRelease)
        {
            triggerWaitRelease = true;
            Log("%s trigger held back until released", HandName(tacticalHand));
        }
        triggerWaitRelease = triggerWaitRelease && rawPressed;
        if (reloadBlock != triggerReloadBlocked)
            Log("%s trigger %s", HandName(tacticalHand), reloadBlock ? "blocked (reload)" : "no longer blocked by the reload");
        triggerReloadBlocked = reloadBlock;

        bool tacticalBound = tacticalButton.active;
        if (!tacticalBound && !reloadBlock && !triggerWaitRelease)
        {
            triggerApplies = false;
            return;
        }
        if (tacticalBound)
        {
            bool block = config.blockTrigger && menu == 0;
            if (block != triggerBlocked)
                Log("%s trigger %s", HandName(tacticalHand), block ? "blocked (gameplay, tactical bound)" : "passes through");
            triggerBlocked = block;
        }
        bool blocked = (tacticalBound && triggerBlocked) || reloadBlock || triggerWaitRelease;
        float output = std::max(tacticalButton.pressed ? 1.0f : 0.0f, rawActive && !blocked ? raw.currentState : 0.0f);
        triggerChanged = !triggerApplies || output != triggerOutput;
        if (triggerChanged)
            triggerChangeTime = tacticalButton.changed || !rawActive ? tacticalButton.changeTime : raw.lastChangeTime;
        if (tacticalButton.changed)
            Log("tactical drives %s trigger = %.1f", HandName(tacticalHand), output);
        triggerOutput = output;
        triggerApplies = true;
    }

    void RecordAim(XrSpace space, XrSpace base, XrTime time, XrSpaceLocationFlags flags, const XrPosef& pose)
    {
        constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        auto it = std::find_if(aimSpaces.begin(), aimSpaces.end(), [space](const auto& aimSpace) { return aimSpace.first == space; });
        if (it == aimSpaces.end() || it->second != gunHand || (flags & valid) != valid || time < gun.time)
            return;
        gun = {true, pose.position.y, base, time};
    }

    void RecordHead(XrSpace base, XrTime time, const XrView* views, uint32_t count)
    {
        if (count == 0 || time < head.time)
            return;
        float sum = 0.0f;
        for (uint32_t i = 0; i < count; ++i)
            sum += views[i].pose.position.y;
        head = {true, sum / static_cast<float>(count), base, time};
    }

    void UpdateSprint(XrSession session, const Config& config)
    {
        sprintChanged = false;
        if (stickClick == XR_NULL_HANDLE)
            return;

        bool sameBase = gun.base == head.base;
        if (gun.valid && head.valid && !sameBase && !loggedSpaceMismatch)
        {
            loggedSpaceMismatch = true;
            Log("gun and head are located in different spaces, sprint disabled");
        }
        bool measured = gun.valid && head.valid && sameBase;
        float belowEyes = measured ? (head.height - gun.height) * 100.0f : 0.0f;
        bool nowLowered = measured && (lowered ? belowEyes >= static_cast<float>(config.sprintStopCm) : belowEyes > static_cast<float>(config.sprintStartCm));
        if (nowLowered != lowered)
            Log("gun %s: %.0f cm below eyes", nowLowered ? "lowered" : "raised", belowEyes);
        if (nowLowered)
            stopStart = stopEnd = 0;
        else if (lowered && measured && menu == 0 && !reloadWaiting)
        {
            stopStart = frame + kSprintStopGapFrames;
            stopEnd = stopStart + kSprintStopFrames;
            Log("sprint stop by stick");
        }
        lowered = nowLowered;
        bool stopping = stopEnd != 0 && frame >= stopStart && frame < stopEnd;
        if (stopEnd != 0 && frame >= stopEnd)
            stopStart = stopEnd = 0;

        bool pressed = lowered && menu == 0 && !reloadWaiting;
        stickChanged = stopping != stickZeroed;
        stickZeroed = stopping;
        if (pressed != sprintPressed)
            Log("sprint %s", pressed ? "pressed" : "released");
        sprintPressed = pressed;

        XrActionStateBoolean raw{XR_TYPE_ACTION_STATE_BOOLEAN};
        bool rawPressed = ReadBoolean(session, stickClick, left, raw) && raw.isActive && raw.currentState;
        bool output = rawPressed || pressed;
        sprintChanged = output != sprintOutput;
        if (sprintChanged)
            sprintChangeTime = rawPressed != sprintOutput && raw.lastChangeTime != 0 ? raw.lastChangeTime : gun.time;
        sprintOutput = output;
    }

    void Sync(XrSession session, const Config& config, std::vector<const char*>& commands)
    {
        ++frame;
        UpdateMenu();
        UpdateReload();
        if (squeeze != XR_NULL_HANDLE)
        {
            UpdateSqueeze(session);
            UpdateGunHand();
            UpdateGrenade(config.grenadeToggle);
        }
        UpdateCrouch(session, config, commands);
        UpdateButton(session, tactical, tacticalButton, "tactical");
        UpdateTacticalHand();
        UpdateTrigger(session, config);
        UpdateSprint(session, config);
    }
};

std::mutex g_mutex;
bool g_active = false;
Config g_config;
Layer g_layer;

XrResult XRAPI_CALL HookCreateActionSet(XrInstance instance, const XrActionSetCreateInfo* info, XrActionSet* set)
{
    XrResult result = g_layer.next.createActionSet(instance, info, set);
    if (XR_FAILED(result) || !info || std::strcmp(info->actionSetName, "gameplay") != 0)
        return result;
    std::lock_guard lock(g_mutex);
    g_layer.tactical = g_layer.CreateAction(*set, "tf2vr_tactical", "Tactical Ability (Cloak)", false);
    g_layer.crouch = g_layer.CreateAction(*set, "tf2vr_crouch", "Crouch", false);
    if (g_config.reloadGrab)
        g_layer.reloadGrab = g_layer.CreateAction(*set, "tf2vr_reload_grab", "Reload Grab", true);
    return result;
}

XrResult XRAPI_CALL HookCreateAction(XrActionSet set, const XrActionCreateInfo* info, XrAction* action)
{
    XrResult result = g_layer.next.createAction(set, info, action);
    if (XR_FAILED(result) || !info)
        return result;
    Log("action '%s' type %d subaction paths %u", info->actionName, info->actionType, info->countSubactionPaths);
    auto is = [info](XrActionType type, const char* name) { return info->actionType == type && std::strcmp(info->actionName, name) == 0; };
    std::lock_guard lock(g_mutex);
    Layer& l = g_layer;
    if (is(XR_ACTION_TYPE_POSE_INPUT, "aim"))
        l.aim = *action;
    else if (is(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary"))
        l.jump = *action;
    else if (g_config.sprint && is(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick"))
        l.stick = *action;
    else if (g_config.sprint && is(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click"))
        l.stickClick = *action;
    else if (is(XR_ACTION_TYPE_FLOAT_INPUT, "trigger"))
        l.trigger = *action;
    else if (is(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze"))
    {
        l.squeeze = *action;
        l.grips.clear();
        for (uint32_t i = 0; i < info->countSubactionPaths; ++i)
        {
            XrPath path = info->subactionPaths[i];
            if (path == l.left || path == l.right)
                l.grips.push_back({path, l.HandName(path)});
        }
    }
    return result;
}

XrResult XRAPI_CALL HookCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* info, XrSpace* space)
{
    XrResult result = g_layer.next.createActionSpace(session, info, space);
    if (XR_FAILED(result) || !info || !space)
        return result;
    std::lock_guard lock(g_mutex);
    if (g_layer.aim != XR_NULL_HANDLE && info->action == g_layer.aim)
        g_layer.aimSpaces.emplace_back(*space, info->subactionPath);
    return result;
}

XrResult XRAPI_CALL HookLocateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* location)
{
    XrResult result = g_layer.next.locateSpace(space, base, time, location);
    if (XR_FAILED(result) || !location)
        return result;
    std::lock_guard lock(g_mutex);
    g_layer.RecordAim(space, base, time, location->locationFlags, location->pose);
    return result;
}

XrResult XRAPI_CALL HookLocateSpaces(XrSession session, const XrSpacesLocateInfo* info, XrSpaceLocations* locations)
{
    XrResult result = g_layer.next.locateSpaces(session, info, locations);
    if (XR_FAILED(result) || !info || !locations || !locations->locations)
        return result;
    std::lock_guard lock(g_mutex);
    for (uint32_t i = 0; i < std::min(info->spaceCount, locations->locationCount); ++i)
        g_layer.RecordAim(info->spaces[i], info->baseSpace, info->time, locations->locations[i].locationFlags, locations->locations[i].pose);
    return result;
}

XrResult XRAPI_CALL HookLocateViews(XrSession session, const XrViewLocateInfo* info, XrViewState* viewState, uint32_t capacity, uint32_t* count, XrView* views)
{
    XrResult result = g_layer.next.locateViews(session, info, viewState, capacity, count, views);
    if (XR_FAILED(result) || !info || !viewState || !count || !views || capacity == 0 || !(viewState->viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
        return result;
    std::lock_guard lock(g_mutex);
    g_layer.RecordHead(info->space, info->displayTime, views, std::min(*count, capacity));
    return result;
}

XrResult XRAPI_CALL HookSyncActions(XrSession session, const XrActionsSyncInfo* info)
{
    XrResult result = g_layer.next.syncActions(session, info);
    if (XR_FAILED(result))
        return result;
    std::vector<const char*> commands;
    {
        std::lock_guard lock(g_mutex);
        g_layer.Sync(session, g_config, commands);
    }
    for (const char* command : commands)
        Log("command '%s' %s", command, GameCommand(command) ? "sent" : "not sent");
    return result;
}

XrResult XRAPI_CALL HookGetActionStateFloat(XrSession session, const XrActionStateGetInfo* info, XrActionStateFloat* state)
{
    XrResult result = g_layer.next.getActionStateFloat(session, info, state);
    if (XR_FAILED(result) || !info || !state)
        return result;
    std::lock_guard lock(g_mutex);
    Layer& l = g_layer;
    if (info->action == l.trigger)
    {
        if (l.triggerApplies && info->subactionPath == l.tacticalHand)
            Override(state, l.triggerOutput, l.triggerChanged, l.triggerChangeTime);
    }
    else if (info->action == l.squeeze)
    {
        const Grip* grip = l.FindGrip(info->subactionPath);
        if (grip && grip->sampled)
            Override(state, grip->output, grip->changed, grip->changeTime);
    }
    return result;
}

XrResult XRAPI_CALL HookGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* info, XrActionStateBoolean* state)
{
    XrResult result = g_layer.next.getActionStateBoolean(session, info, state);
    if (XR_FAILED(result) || !info || !state)
        return result;
    std::lock_guard lock(g_mutex);
    const Layer& l = g_layer;
    if (info->action == l.jump && info->subactionPath == l.right)
    {
        if (l.jumpOverride || l.jumpChanged)
            Override(state, l.jumpOutput, l.jumpChanged, 0);
    }
    else if (info->action == l.stickClick && info->subactionPath == l.left)
    {
        if (l.sprintPressed || l.sprintChanged)
            Override(state, l.sprintOutput, l.sprintChanged, l.sprintChangeTime);
    }
    return result;
}

XrResult XRAPI_CALL HookGetActionStateVector2f(XrSession session, const XrActionStateGetInfo* info, XrActionStateVector2f* state)
{
    XrResult result = g_layer.next.getActionStateVector2f(session, info, state);
    if (XR_FAILED(result) || !info || !state)
        return result;
    std::lock_guard lock(g_mutex);
    if (info->action != g_layer.stick || info->subactionPath != g_layer.left)
        return result;
    if (g_layer.stickZeroed)
        state->currentState = {0.0f, 0.0f};
    if (g_layer.stickChanged)
        state->changedSinceLastSync = XR_TRUE;
    return result;
}

XrResult XRAPI_CALL LayerGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
{
    if (!name || !function)
        return XR_ERROR_VALIDATION_FAILURE;
    static const std::pair<const char*, PFN_xrVoidFunction> hooks[] = {
        {"xrGetInstanceProcAddr", reinterpret_cast<PFN_xrVoidFunction>(&LayerGetInstanceProcAddr)},
        {"xrCreateActionSet", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateActionSet)},
        {"xrCreateAction", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateAction)},
        {"xrSyncActions", reinterpret_cast<PFN_xrVoidFunction>(&HookSyncActions)},
        {"xrGetActionStateFloat", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateFloat)},
        {"xrGetActionStateBoolean", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateBoolean)},
        {"xrGetActionStateVector2f", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateVector2f)},
        {"xrCreateActionSpace", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateActionSpace)},
        {"xrLocateSpace", reinterpret_cast<PFN_xrVoidFunction>(&HookLocateSpace)},
        {"xrLocateViews", reinterpret_cast<PFN_xrVoidFunction>(&HookLocateViews)},
    };
    if (g_active)
    {
        for (const auto& [hookName, hook] : hooks)
        {
            if (std::strcmp(hookName, name) == 0)
            {
                *function = hook;
                return XR_SUCCESS;
            }
        }
        if (std::strcmp(name, "xrLocateSpaces") == 0 && g_layer.next.locateSpaces)
        {
            *function = reinterpret_cast<PFN_xrVoidFunction>(&HookLocateSpaces);
            return XR_SUCCESS;
        }
    }
    if (!g_layer.next.getInstanceProcAddr)
    {
        *function = nullptr;
        return XR_ERROR_HANDLE_INVALID;
    }
    return g_layer.next.getInstanceProcAddr(instance, name, function);
}

XrResult XRAPI_CALL LayerCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo, XrInstance* instance)
{
    if (!layerInfo || !layerInfo->nextInfo || !layerInfo->nextInfo->nextGetInstanceProcAddr || !layerInfo->nextInfo->nextCreateApiLayerInstance)
        return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo chained = *layerInfo;
    chained.nextInfo = layerInfo->nextInfo->next;
    XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &chained, instance);
    if (XR_FAILED(result))
        return result;

    std::lock_guard lock(g_mutex);
    g_layer = Layer{};
    g_layer.instance = *instance;
    g_layer.next.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    if (!g_active)
        return result;
    if (!g_layer.Resolve())
    {
        Log("layer disabled: next functions unavailable");
        g_active = false;
        return result;
    }
    g_layer.left = g_layer.ToPath("/user/hand/left");
    g_layer.right = g_layer.ToPath("/user/hand/right");
    g_layer.gunHand = g_layer.right;
    g_layer.tacticalHand = g_layer.left;
    return result;
}

void Initialize()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    g_active = _wcsicmp(slash ? slash + 1 : path, L"Titanfall2VRLauncher.exe") == 0;
    if (!g_active)
        return;
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Initialize), &module);
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring directory = path;
    directory.resize(directory.find_last_of(L"\\/") + 1);
    g_config = LoadConfig(directory + L"TF2VRBetterBindings.ini");
    LogOpen(directory + L"TF2VRBetterBindings.log");
    Log("config: reload_grab=%d grenade_toggle=%d crouch_mode=%s uncrouch_on_jump=%d jump_delay_ms=%d block_hand_trigger_in_game=%d sprint=%d "
        "start_below_eyes_cm=%d stop_below_eyes_cm=%d",
        g_config.reloadGrab, g_config.grenadeToggle, g_config.crouchButtonMode ? "button" : "toggle", g_config.uncrouchOnJump, g_config.jumpDelayMs,
        g_config.blockTrigger, g_config.sprint, g_config.sprintStartCm, g_config.sprintStopCm);
}
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo, const char*, XrNegotiateApiLayerRequest* request)
{
    if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) || request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION || request->structSize != sizeof(XrNegotiateApiLayerRequest) ||
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION || loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;
    XrVersion apiVersion = std::min<XrVersion>(XR_CURRENT_API_VERSION, loaderInfo->maxApiVersion);
    if (apiVersion < loaderInfo->minApiVersion)
        return XR_ERROR_INITIALIZATION_FAILED;

    static std::once_flag once;
    std::call_once(once, Initialize);
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = apiVersion;
    request->getInstanceProcAddr = LayerGetInstanceProcAddr;
    request->createApiLayerInstance = LayerCreateApiLayerInstance;
    return XR_SUCCESS;
}
