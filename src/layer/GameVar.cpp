#include "GameVar.h"

#include "Log.h"

#include <cstddef>
#include <cstring>

namespace tf2vr
{
namespace
{
using CreateInterfaceFn = void* (*)(const char* name, int* returnCode);
using FindVarFn = void* (*)(void* self, const char* name);

constexpr size_t kFindVarIndex = 16;
constexpr size_t kNameOffset = 0x18;
constexpr size_t kIntValueOffset = 0x5C;
constexpr ULONGLONG kRetryIntervalMs = 1000;

enum FindStatus
{
    kFound,
    kNoVstdlib,
    kNoCreateInterface,
    kNoCvarInterface,
    kNotRegistered,
    kNameMismatch,
    kFault,
};

const char* StatusText(int status)
{
    switch (status)
    {
    case kFound: return "found";
    case kNoVstdlib: return "vstdlib.dll not loaded";
    case kNoCreateInterface: return "vstdlib.dll has no CreateInterface";
    case kNoCvarInterface: return "VEngineCvar007 unavailable";
    case kNotRegistered: return "not registered yet";
    case kNameMismatch: return "FindVar returned a different variable";
    default: return "access fault while resolving";
    }
}

void* FindConVarGuarded(const char* name, int* status)
{
    __try
    {
        HMODULE vstdlib = GetModuleHandleW(L"vstdlib.dll");
        if (!vstdlib)
        {
            *status = kNoVstdlib;
            return nullptr;
        }
        auto createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(vstdlib, "CreateInterface"));
        if (!createInterface)
        {
            *status = kNoCreateInterface;
            return nullptr;
        }
        void* cvar = createInterface("VEngineCvar007", nullptr);
        if (!cvar)
        {
            *status = kNoCvarInterface;
            return nullptr;
        }
        auto findVar = reinterpret_cast<FindVarFn>((*static_cast<void***>(cvar))[kFindVarIndex]);
        void* conVar = findVar(cvar, name);
        if (!conVar)
        {
            *status = kNotRegistered;
            return nullptr;
        }
        const char* actual = *reinterpret_cast<const char* const*>(static_cast<unsigned char*>(conVar) + kNameOffset);
        if (!actual || std::strcmp(actual, name) != 0)
        {
            *status = kNameMismatch;
            return nullptr;
        }
        *status = kFound;
        return conVar;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        *status = kFault;
        return nullptr;
    }
}
}

bool GameVar::ReadInt(int& value)
{
    if (!m_conVar)
    {
        ULONGLONG now = GetTickCount64();
        if (now < m_nextAttempt)
            return false;
        m_nextAttempt = now + kRetryIntervalMs;

        int status = kFault;
        void* conVar = FindConVarGuarded(m_name, &status);
        if (status != m_lastStatus)
        {
            m_lastStatus = status;
            Log("game var %s: %s", m_name, StatusText(status));
        }
        if (!conVar)
            return false;
        m_conVar = static_cast<unsigned char*>(conVar);
    }
    value = *reinterpret_cast<const volatile int*>(m_conVar + kIntValueOffset);
    return true;
}
}
