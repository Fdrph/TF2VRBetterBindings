#include "Config.h"

#include <windows.h>

#include <iterator>

namespace tf2vr
{
namespace
{
std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& fallback)
{
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(section, key, fallback.c_str(), buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

std::string Narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
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

std::string ReadNarrow(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::string& fallback)
{
    return Narrow(ReadString(path, section, key, Widen(fallback)));
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback)
{
    return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, path.c_str()) != 0;
}
}

Config LoadConfig(const std::wstring& path)
{
    Config config;
    config.targetProcess = ReadString(path, L"general", L"target_process", config.targetProcess);
    config.toggleLeft = ReadBool(path, L"grip", L"toggle_left", config.toggleLeft);
    config.toggleRight = ReadBool(path, L"grip", L"toggle_right", config.toggleRight);
    config.crouchEnabled = ReadBool(path, L"crouch", L"enabled", config.crouchEnabled);
    config.crouchBinding = ReadNarrow(path, L"crouch", L"binding", config.crouchBinding);
    config.crouchPressCommand = ReadNarrow(path, L"crouch", L"press_command", config.crouchPressCommand);
    config.crouchReleaseCommand = ReadNarrow(path, L"crouch", L"release_command", config.crouchReleaseCommand);
    config.tacticalEnabled = ReadBool(path, L"tactical", L"enabled", config.tacticalEnabled);
    config.tacticalHand = ReadNarrow(path, L"tactical", L"hand", config.tacticalHand);
    config.tacticalBlockHandTrigger = ReadBool(path, L"tactical", L"block_hand_trigger_in_game", config.tacticalBlockHandTrigger);
    config.tacticalPressCommand = ReadNarrow(path, L"tactical", L"press_command", config.tacticalPressCommand);
    config.tacticalReleaseCommand = ReadNarrow(path, L"tactical", L"release_command", config.tacticalReleaseCommand);
    return config;
}
}
