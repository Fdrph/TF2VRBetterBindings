#pragma once

#include <string>

namespace tf2vr
{
struct Config
{
    std::wstring targetProcess = L"Titanfall2VRLauncher.exe";
    bool toggleLeft = true;
    bool toggleRight = true;
    bool crouchEnabled = true;
    std::string crouchBinding;
    std::string crouchPressCommand = "+duck";
    std::string crouchReleaseCommand = "-duck";
    bool tacticalEnabled = true;
    std::string tacticalHand = "left";
    bool tacticalBlockHandTrigger = true;
    std::string tacticalPressCommand;
    std::string tacticalReleaseCommand;
};

Config LoadConfig(const std::wstring& path);
}
