#pragma once

#include <string>

namespace tf2vr
{
struct Config
{
    std::wstring targetProcess = L"Titanfall2VRLauncher.exe";
    bool toggleLeft = true;
    bool toggleRight = true;
    bool reloadGuard = true;
    std::string lockMode = "toggle";
    bool crouchEnabled = true;
    std::string crouchBinding;
    std::string crouchPressCommand = "+duck";
    std::string crouchReleaseCommand = "-duck";
    std::string crouchMode = "toggle";
    bool crouchUncrouchOnJump = true;
    int crouchJumpDelayMs = 250;
    bool tacticalEnabled = true;
    std::string tacticalHand = "auto";
    bool tacticalBlockHandTrigger = true;
    std::string tacticalPressCommand;
    std::string tacticalReleaseCommand;
    bool sprintEnabled = true;
    std::string sprintHand = "left";
    int sprintStartBelowEyesCm = 30;
    int sprintStopBelowEyesCm = 20;
    std::string sprintMode = "hold";
    std::string sprintStopMethod = "stick";
    int sprintStopFrames = 6;
};

Config LoadConfig(const std::wstring& path);
}
