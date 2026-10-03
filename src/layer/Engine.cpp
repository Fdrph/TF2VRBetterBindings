#include "Engine.h"

#include "Log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace tf2vr
{
namespace
{
using CbufAddTextFn = void (*)(int target, const char* text, int source);

constexpr uintptr_t kCbufAddTextRva = 0x1203B0;
constexpr int kCommandTargetFirstPlayer = 0;
constexpr int kCommandSourceCode = 0;
constexpr unsigned char kCbufAddTextPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xD9, 0x48, 0x8D, 0x0D,
};

std::mutex g_engineMutex;
bool g_resolved = false;
CbufAddTextFn g_cbufAddText = nullptr;

void ResolveLocked()
{
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    if (!engine)
    {
        Log("engine: engine.dll is not loaded");
        return;
    }

    g_resolved = true;
    auto base = reinterpret_cast<const unsigned char*>(engine);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    Log("engine: engine.dll at %p timestamp 0x%08lx size 0x%lx", engine, nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);

    if (nt->OptionalHeader.SizeOfImage < kCbufAddTextRva + sizeof(kCbufAddTextPrologue))
    {
        Log("engine: image too small for Cbuf_AddText, console commands disabled");
        return;
    }
    if (std::memcmp(base + kCbufAddTextRva, kCbufAddTextPrologue, sizeof(kCbufAddTextPrologue)) != 0)
    {
        Log("engine: Cbuf_AddText prologue mismatch, console commands disabled");
        return;
    }

    g_cbufAddText = reinterpret_cast<CbufAddTextFn>(const_cast<unsigned char*>(base + kCbufAddTextRva));
    Log("engine: Cbuf_AddText verified at %p", g_cbufAddText);
}
}

bool EngineExecute(const char* command)
{
    CbufAddTextFn addText = nullptr;
    {
        std::lock_guard lock(g_engineMutex);
        if (!g_resolved)
            ResolveLocked();
        addText = g_cbufAddText;
    }
    if (!addText)
        return false;

    std::string line = command;
    line += '\n';
    addText(kCommandTargetFirstPlayer, line.c_str(), kCommandSourceCode);
    return true;
}
}
