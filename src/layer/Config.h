#pragma once

#include <string>

namespace tf2vr
{
struct Config
{
    std::wstring targetProcess = L"Titanfall2VRLauncher.exe";
    bool reloadGrabEnabled = true;
    bool grenadeToggle = false;
    bool crouchEnabled = true;
    std::string crouchPressCommand = "+duck";
    std::string crouchReleaseCommand = "-duck";
    std::string crouchMode = "toggle";
    bool crouchUncrouchOnJump = false;
    int crouchJumpDelayMs = 250;
    bool tacticalEnabled = true;
    std::string tacticalHand = "auto";
    bool tacticalBlockHandTrigger = true;
    std::string tacticalPressCommand;
    std::string tacticalReleaseCommand;
    bool sprintEnabled = false;
    std::string sprintHand = "left";
    int sprintStartBelowEyesCm = 30;
    int sprintStopBelowEyesCm = 20;
    std::string sprintMode = "hold";
    std::string sprintStopMethod = "stick";
    int sprintStopFrames = 6;
    bool weaponSwitchEnabled = true;
    std::string weaponSwitchMethod = "grab";
    int weaponSwitchReleaseFrames = 4;
    int weaponSwitchMoveFrames = 3;
    int weaponSwitchGrabFrames = 4;
    int weaponSwitchTimeoutMs = 750;
    bool holsterLearn = true;
    bool holsterAssist = true;
    int holsterAssistRadiusCm = 15;
};

Config LoadConfig(const std::wstring& path);
}
