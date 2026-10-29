#pragma once

#include <string>

namespace tf2vr
{
// tf2vrbb_trigger_kind values, set by the companion client script
constexpr int kTriggerKindCount = 8;
constexpr const char* kTriggerKindNames[kTriggerKindCount] = {"none", "pistol", "auto", "marksman", "shotgun", "launcher", "arc_tool", "titan"};

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
    std::string weaponSwitchCycle = "primary,secondary,arc_tool,empty";
    bool holsterLearn = true;
    bool holsterAssist = true;
    int holsterAssistRadiusCm = 15;
    bool detonateEnabled = true;
    bool multiplayerButtonDraws = false;
    bool multiplayerAssist = false;
    bool triggersEnabled = true;
    std::wstring triggersCapiPath;
    // adaptive trigger profile per tf2vrbb_trigger_kind, "<mode> <parameters>", see TriggerHaptics.h
    std::string triggerProfiles[kTriggerKindCount] = {"off", "weapon 4 6 7", "feedback 3 3", "weapon 5 7 8", "weapon 3 6 8", "slope 2 8 3 8", "feedback 2 2", "slope 1 8 4 8"};
    // played instead while the gun is firing; empty = keep the profile
    std::string triggerFiring[kTriggerKindCount] = {"", "", "vibration 3 5 30", "", "", "", "", "vibration 2 6 15"};
    std::string triggerEmpty = "off";
    int triggerFiringMs = 150;
};

Config LoadConfig(const std::wstring& path);
}
