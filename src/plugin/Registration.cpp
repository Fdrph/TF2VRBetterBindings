#include "Registration.h"

#include <iterator>
#include <vector>

namespace tf2vrbb
{
namespace
{
std::wstring FileName(const std::wstring& path)
{
    return path.substr(path.find_last_of(L"\\/") + 1);
}
}

RegistrationReport RegisterImplicitLayer(HKEY root, const std::wstring& subkey, const std::wstring& manifestPath)
{
    RegistrationReport report;
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return report;

    std::vector<std::wstring> stale;
    wchar_t name[16384];
    for (DWORD index = 0;; ++index)
    {
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumValueW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        if (_wcsicmp(FileName(name).c_str(), FileName(manifestPath).c_str()) == 0 && _wcsicmp(name, manifestPath.c_str()) != 0)
            stale.emplace_back(name, length);
    }
    for (const std::wstring& value : stale)
        report.removedStale += RegDeleteValueW(key, value.c_str()) == ERROR_SUCCESS;

    DWORD value = 1;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, manifestPath.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD)
    {
        report.result = value == 0 ? RegistrationResult::AlreadyRegistered : RegistrationResult::DisabledByUser;
    }
    else
    {
        DWORD enabled = 0;
        bool written = RegSetValueExW(key, manifestPath.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled)) == ERROR_SUCCESS;
        report.result = written ? RegistrationResult::Registered : RegistrationResult::Failed;
    }
    RegCloseKey(key);
    return report;
}
}
