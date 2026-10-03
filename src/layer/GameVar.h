#pragma once

#include <windows.h>

namespace tf2vr
{
class GameVar
{
public:
    explicit GameVar(const char* name) : m_name(name) {}

    bool ReadInt(int& value);

private:
    const char* m_name;
    unsigned char* m_conVar = nullptr;
    ULONGLONG m_nextAttempt = 0;
    int m_lastStatus = -1;
};
}
