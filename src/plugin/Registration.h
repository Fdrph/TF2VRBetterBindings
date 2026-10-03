#pragma once

#include <windows.h>

#include <string>

namespace tf2vrbb
{
enum class RegistrationResult
{
    AlreadyRegistered,
    Registered,
    DisabledByUser,
    Failed,
};

struct RegistrationReport
{
    RegistrationResult result = RegistrationResult::Failed;
    int removedStale = 0;
};

RegistrationReport RegisterImplicitLayer(HKEY root, const std::wstring& subkey, const std::wstring& manifestPath);
int UnregisterImplicitLayer(HKEY root, const std::wstring& subkey, const std::wstring& manifestFileName);
}
