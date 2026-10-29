#include "Config.h"
#include "Engine.h"
#include "GameVar.h"
#include "Log.h"
#include "TriggerHaptics.h"

#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace tf2vr
{
namespace
{
constexpr const wchar_t* kConfigFileName = L"TF2VRBetterBindings.ini";
constexpr const wchar_t* kLogFileName = L"TF2VRBetterBindings.log";
constexpr const char* kMenuActiveVar = "tf2vrbb_menu_active";
constexpr const char* kStartupPhaseVar = "tf2vrbb_startup_phase";
constexpr const char* kReloadHighlightVar = "tf2vr_reload_highlight";
constexpr const char* kGunAckVar = "tf2vr_gun_ack";
constexpr const char* kGrenadeAckVar = "tf2vr_grenade_ack";
constexpr const char* kGunGripToggleVar = "tf2vr_toggle_grip";
constexpr float kHeldThreshold = 0.5f;
constexpr const char* kGameplaySetName = "gameplay";
constexpr const char* kSqueezeActionName = "squeeze";
constexpr const char* kTriggerActionName = "trigger";
constexpr const char* kAimActionName = "aim";
constexpr const char* kStickClickActionName = "stick_click";
constexpr const char* kStickActionName = "stick";
constexpr const char* kJumpActionName = "primary";
constexpr uint64_t kSprintStopGapFrames = 2;
constexpr uint64_t kJumpPulseFrames = 6;
constexpr uint64_t kSprintPulseFrames = 8;
constexpr uint64_t kPitchLogFrames = 90;
constexpr const char* kCrouchActionName = "tf2vr_crouch";
constexpr const char* kCrouchLocalizedName = "Crouch";
constexpr const char* kTacticalActionName = "tf2vr_tactical";
constexpr const char* kTacticalLocalizedName = "Tactical Ability (Cloak)";
constexpr const char* kReloadGrabActionName = "tf2vr_reload_grab";
constexpr const char* kReloadGrabLocalizedName = "Reload Grab";
constexpr std::chrono::milliseconds kGunGrabWindow{500};
constexpr std::chrono::milliseconds kReloadEndGrace{200};
constexpr std::chrono::milliseconds kGrenadeMinPress{150};
constexpr int kMaxLoggedQueries = 8;
constexpr const char* kHolsterMaskVar = "tf2vr_holster_mask";
constexpr const char* kGunSlotVar = "tf2vr_gun_slot";
constexpr const char* kActiveSlotVar = "tf2vrbb_active_slot";
constexpr const char* kTriggerKindVar = "tf2vrbb_trigger_kind";
constexpr const char* kClipVar = "tf2vrbb_clip";
constexpr const char* kMultiplayerVar = "tf2vrbb_multiplayer";
constexpr const char* kDetonateActionName = "tf2vr_detonate";
constexpr const char* kDetonateLocalizedName = "Detonate Satchels";
constexpr const char* kDetonateCommand = "tf2vr_detonate";
constexpr const char* kPickupLeftVar = "tf2vr_pickup_left";
constexpr const char* kPickupRightVar = "tf2vr_pickup_right";
constexpr const wchar_t* kHolsterFileName = L"TF2VRBetterBindings.holsters.ini";
constexpr const char* kNextWeaponActionName = "tf2vr_next_weapon";
constexpr const char* kNextWeaponLocalizedName = "Next Weapon";
constexpr const char* kDrawPrimaryActionName = "tf2vr_draw_primary";
constexpr const char* kDrawPrimaryLocalizedName = "Draw Primary Weapon";
constexpr const char* kDrawSecondaryActionName = "tf2vr_draw_secondary";
constexpr const char* kDrawSecondaryLocalizedName = "Draw Secondary Weapon";
// holster bits in tf2vr_holster_mask: gun slots 0, 1, 3 (arc tool) and 4 while a grenade is ready
constexpr int kGrenadeSlot = 4;
constexpr int kHolsterSlots[] = {0, 1, 3, kGrenadeSlot};
constexpr const char* kHolsterNames[] = {"primary", "secondary", "arc_tool", "grenade"};
constexpr int kHolsterCount = 4;
constexpr int kGunSlots[] = {0, 1, 3};
constexpr float kHolsterLearnRate = 0.3f;
constexpr int kTogglePulseFrames = 3;
constexpr int kCommandTicketBase = 1000000;
// tf2vrbb_active_slot before the companion client script runs
constexpr int kActiveSlotUnknown = -2;
// switch target that only holsters the gun in hand
constexpr int kEmptyHandsSlot = -3;

struct NextFunctions
{
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
    PFN_xrStringToPath stringToPath = nullptr;
    PFN_xrPathToString pathToString = nullptr;
    PFN_xrCreateActionSet createActionSet = nullptr;
    PFN_xrCreateAction createAction = nullptr;
    PFN_xrSyncActions syncActions = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f = nullptr;
    PFN_xrCreateReferenceSpace createReferenceSpace = nullptr;
    PFN_xrCreateActionSpace createActionSpace = nullptr;
    PFN_xrLocateSpace locateSpace = nullptr;
    PFN_xrLocateSpaces locateSpaces = nullptr;
    PFN_xrLocateViews locateViews = nullptr;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 FromXr(const XrVector3f& v) { return {v.x, v.y, v.z}; }

Vec3 Rotate(const XrQuaternionf& q, Vec3 v)
{
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}

struct AimSpace
{
    XrSpace space = XR_NULL_HANDLE;
    XrPath hand = XR_NULL_PATH;
};

// any pose action space of a hand; all of them move together while a grab is faked
struct HandSpace
{
    XrSpace space = XR_NULL_HANDLE;
    XrPath hand = XR_NULL_PATH;
};

struct HandPose
{
    bool valid = false;
    Vec3 position;
    XrSpace base = XR_NULL_HANDLE;
    XrTime time = 0;
};

// eye midpoint with the head's heading flattened onto the floor; holsters are stored in this frame
struct HeadFrame
{
    bool valid = false;
    Vec3 position;
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    XrSpace base = XR_NULL_HANDLE;
    XrTime time = 0;
};

// right, up and forward of the head, in meters
struct Holster
{
    bool learned = false;
    Vec3 local;
    int samples = 0;
};

enum class SwitchPhase
{
    Idle,
    Release,
    WaitEmpty,
    Move,
    Grab,
    Confirm,
};

// a faked grab: optionally let go of the current gun, move the hand to a holster, squeeze there
struct WeaponSwitch
{
    SwitchPhase phase = SwitchPhase::Idle;
    bool assist = false;
    bool toggle = false;
    XrPath hand = XR_NULL_PATH;
    int slot = -1;
    Vec3 target;
    int frame = 0;
    std::chrono::steady_clock::time_point deadline;
    int startGunAck = 0;
    int startGrenadeAck = 0;
    float squeeze = -1.0f;
    bool spoof = false;
    bool loggedSpoof = false;
};

struct SprintState
{
    bool lowered = false;
    bool pressed = false;
    uint64_t pulseEnd = 0;
    uint64_t stopStart = 0;
    uint64_t stopEnd = 0;
    bool stickZeroed = false;
    bool stickChanged = false;
    bool output = false;
    bool changed = false;
    XrTime changeTime = 0;
};

struct GripSlot
{
    XrPath path = XR_NULL_PATH;
    const char* name = "";
    bool sampled = false;
    bool routed = false;
    float output = 0.0f;
    bool changed = false;
    XrTime changeTime = 0;
    bool held = false;
    std::chrono::steady_clock::time_point heldAt;
    bool grabBound = false;
    bool rawPressed = false;
    std::chrono::steady_clock::time_point pressAt;
    bool grenadeHeld = false;
    bool waitRelease = false;
    bool forceHeld = false;
    bool pressValid = false;
    Vec3 pressLocal;
    bool pressPickup = false;
    bool pressSynthetic = false;
};

struct ButtonState
{
    bool active = false;
    bool pressed = false;
    bool changed = false;
    XrTime changeTime = 0;
};

struct CrouchState
{
    bool jumpPressed = false;
    bool inverted = false;
    bool released = false;
    bool ducked = false;
    bool jumpPending = false;
    std::chrono::steady_clock::time_point jumpReleaseAt;
    uint64_t jumpPulseEnd = 0;
    bool jumpOverride = false;
    bool jumpOutput = false;
    bool jumpChanged = false;
};

struct TriggerOverride
{
    bool applies = false;
    bool blocked = false;
    bool reloadBlocked = false;
    bool waitRelease = false;
    float output = 0.0f;
    bool changed = false;
    XrTime changeTime = 0;
};

struct LayerState
{
    std::mutex mutex;
    bool active = false;
    std::wstring directory;
    Config config;
    NextFunctions next;
    XrInstance instance = XR_NULL_HANDLE;
    XrPath leftHand = XR_NULL_PATH;
    XrPath rightHand = XR_NULL_PATH;
    XrPath tacticalHand = XR_NULL_PATH;
    XrAction squeeze = XR_NULL_HANDLE;
    XrAction aim = XR_NULL_HANDLE;
    XrAction stickClick = XR_NULL_HANDLE;
    XrAction stick = XR_NULL_HANDLE;
    XrPath sprintHand = XR_NULL_PATH;
    std::vector<AimSpace> aimSpaces;
    std::vector<std::pair<XrSpace, XrReferenceSpaceType>> referenceSpaces;
    bool gunPoseValid = false;
    float gunPitch = 0.0f;
    float gunHeight = 0.0f;
    XrSpace gunPoseBase = XR_NULL_HANDLE;
    XrTime gunPoseTime = 0;
    bool headValid = false;
    float headHeight = 0.0f;
    XrSpace headBase = XR_NULL_HANDLE;
    XrTime headTime = 0;
    uint64_t gunPoseLoggedFrame = 0;
    bool loggedBaseMismatch = false;
    bool loggedAimLocate = false;
    SprintState sprint;
    XrAction trigger = XR_NULL_HANDLE;
    XrAction reloadGrab = XR_NULL_HANDLE;
    XrAction crouch = XR_NULL_HANDLE;
    XrAction tactical = XR_NULL_HANDLE;
    std::vector<GripSlot> grips;
    ButtonState crouchButton;
    CrouchState crouchState;
    XrAction jump = XR_NULL_HANDLE;
    XrPath jumpHand = XR_NULL_PATH;
    ButtonState tacticalButton;
    TriggerOverride triggerOverride;
    GameVar menuActiveVar{kMenuActiveVar};
    GameVar startupPhaseVar{kStartupPhaseVar};
    GameVar reloadHighlightVar{kReloadHighlightVar};
    GameVar gunAckVar{kGunAckVar};
    GameVar grenadeAckVar{kGrenadeAckVar};
    GameVar gunGripToggleVar{kGunGripToggleVar};
    int menuState = -2;
    int startupPhase = -2;
    int reloadHighlight = 0;
    bool reloadWaiting = false;
    std::chrono::steady_clock::time_point reloadSeenAt;
    bool gunAckKnown = false;
    int gunAck = 0;
    bool grenadeAckKnown = false;
    int grenadeAck = 0;
    bool grenadeToggleActive = false;
    XrPath gunHand = XR_NULL_PATH;
    std::vector<XrAction> poseActions;
    std::vector<HandSpace> handSpaces;
    HandPose hands[2];
    HeadFrame head;
    std::wstring holsterPath;
    Holster holsters[kHolsterCount][2];
    XrAction nextWeapon = XR_NULL_HANDLE;
    XrAction drawPrimary = XR_NULL_HANDLE;
    XrAction drawSecondary = XR_NULL_HANDLE;
    ButtonState nextWeaponButton;
    ButtonState drawPrimaryButton;
    ButtonState drawSecondaryButton;
    GameVar holsterMaskVar{kHolsterMaskVar};
    GameVar gunSlotVar{kGunSlotVar};
    GameVar activeSlotVar{kActiveSlotVar};
    GameVar pickupLeftVar{kPickupLeftVar};
    GameVar pickupRightVar{kPickupRightVar};
    int holsterMask = 0;
    int gunSlot = -1;
    int activeSlot = kActiveSlotUnknown;
    int pickup[2] = {-1, -1};
    WeaponSwitch weaponSwitch;
    std::vector<int> weaponCycle;
    XrAction detonate = XR_NULL_HANDLE;
    ButtonState detonateButton;
    GameVar multiplayerVar{kMultiplayerVar};
    bool multiplayer = false;
    GameVar triggerKindVar{kTriggerKindVar};
    GameVar clipVar{kClipVar};
    TriggerEffect triggerProfiles[kTriggerKindCount];
    TriggerEffect triggerFiring[kTriggerKindCount];
    bool triggerHasFiring[kTriggerKindCount] = {};
    TriggerEffect triggerEmpty;
    int triggerKind = 0;
    int clip = -1;
    std::chrono::steady_clock::time_point firingUntil;
    int commandTicket = kCommandTicketBase;
    bool loggedSpoofBase = false;
    uint64_t frame = 0;
    int loggedSqueezeQueries = 0;
    int loggedBooleanQueries = 0;
    int loggedStickQueries = 0;
    bool loggedFirstSync = false;
};

LayerState& State()
{
    static LayerState state;
    return state;
}

std::wstring ModuleDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&State), &module);
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring result = path;
    size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : result.substr(0, slash);
}

std::wstring ProcessFileName()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result = path;
    size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? result : result.substr(slash + 1);
}

std::string NarrowPath(const std::wstring& text)
{
    std::string result;
    for (wchar_t c : text)
        result += c < 128 ? static_cast<char>(c) : '?';
    return result;
}

std::wstring WidenAscii(const char* text)
{
    std::wstring result;
    for (; *text; ++text)
        result += static_cast<wchar_t>(*text);
    return result;
}

const char* kHandNames[] = {"left", "right"};

std::wstring HolsterSection(int index, int hand)
{
    return L"holster_" + WidenAscii(kHolsterNames[index]) + L"_" + WidenAscii(kHandNames[hand]);
}

void LoadHolsters(LayerState& s)
{
    const wchar_t* keys[] = {L"right_cm", L"up_cm", L"forward_cm"};
    for (int index = 0; index < kHolsterCount; ++index)
    {
        for (int hand = 0; hand < 2; ++hand)
        {
            std::wstring section = HolsterSection(index, hand);
            Holster& holster = s.holsters[index][hand];
            holster.samples = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"samples", 0, s.holsterPath.c_str()));
            float values[3] = {};
            for (int axis = 0; axis < 3; ++axis)
            {
                wchar_t buffer[64] = {};
                GetPrivateProfileStringW(section.c_str(), keys[axis], L"0", buffer, 64, s.holsterPath.c_str());
                values[axis] = std::wcstof(buffer, nullptr) / 100.0f;
            }
            holster.local = {values[0], values[1], values[2]};
            holster.learned = holster.samples > 0;
            if (holster.learned)
                Log("holster %s (%s hand): right %.0f up %.0f forward %.0f cm, %d samples", kHolsterNames[index], kHandNames[hand], values[0] * 100.0f,
                    values[1] * 100.0f, values[2] * 100.0f, holster.samples);
        }
    }
}

void SaveHolster(const LayerState& s, int index, int hand)
{
    const Holster& holster = s.holsters[index][hand];
    std::wstring section = HolsterSection(index, hand);
    const wchar_t* keys[] = {L"right_cm", L"up_cm", L"forward_cm"};
    float values[] = {holster.local.x, holster.local.y, holster.local.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        wchar_t buffer[64] = {};
        swprintf_s(buffer, L"%.1f", values[axis] * 100.0f);
        WritePrivateProfileStringW(section.c_str(), keys[axis], buffer, s.holsterPath.c_str());
    }
    WritePrivateProfileStringW(section.c_str(), L"samples", std::to_wstring(holster.samples).c_str(), s.holsterPath.c_str());
}

void InitializeOnce()
{
    static std::once_flag once;
    std::call_once(once, [] {
        LayerState& s = State();
        s.directory = ModuleDirectory();
        s.config = LoadConfig(s.directory + L"\\" + kConfigFileName);
        std::wstring process = ProcessFileName();
        s.active = _wcsicmp(process.c_str(), s.config.targetProcess.c_str()) == 0;
        if (!s.active)
            return;

        LogOpen(s.directory + L"\\" + kLogFileName);
        Log("layer active in %s", NarrowPath(process).c_str());
        Log("config: reload_grab=%d grenade_toggle=%d", s.config.reloadGrabEnabled, s.config.grenadeToggle);
        Log("config: crouch=%d press_cmd='%s' release_cmd='%s'", s.config.crouchEnabled, s.config.crouchPressCommand.c_str(),
            s.config.crouchReleaseCommand.c_str());
        Log("config: crouch mode=%s uncrouch_on_jump=%d jump_delay_ms=%d", s.config.crouchMode.c_str(), s.config.crouchUncrouchOnJump,
            s.config.crouchJumpDelayMs);
        Log("config: tactical=%d hand=%s block_in_game=%d press_cmd='%s' release_cmd='%s'", s.config.tacticalEnabled, s.config.tacticalHand.c_str(),
            s.config.tacticalBlockHandTrigger, s.config.tacticalPressCommand.c_str(), s.config.tacticalReleaseCommand.c_str());
        Log("config: sprint=%d hand=%s start_below_eyes_cm=%d stop_below_eyes_cm=%d mode=%s stop_method=%s stop_frames=%d", s.config.sprintEnabled,
            s.config.sprintHand.c_str(), s.config.sprintStartBelowEyesCm, s.config.sprintStopBelowEyesCm, s.config.sprintMode.c_str(),
            s.config.sprintStopMethod.c_str(), s.config.sprintStopFrames);
        Log("config: weapon_switch=%d method=%s release_frames=%d move_frames=%d grab_frames=%d timeout_ms=%d cycle=%s",
            s.config.weaponSwitchEnabled, s.config.weaponSwitchMethod.c_str(), s.config.weaponSwitchReleaseFrames, s.config.weaponSwitchMoveFrames,
            s.config.weaponSwitchGrabFrames, s.config.weaponSwitchTimeoutMs, s.config.weaponSwitchCycle.c_str());
        for (size_t begin = 0; begin <= s.config.weaponSwitchCycle.size();)
        {
            size_t comma = s.config.weaponSwitchCycle.find(',', begin);
            if (comma == std::string::npos)
                comma = s.config.weaponSwitchCycle.size();
            std::string name;
            for (char c : s.config.weaponSwitchCycle.substr(begin, comma - begin))
                if (c != ' ' && c != '	')
                    name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            begin = comma + 1;
            int slot = name == "empty" ? kEmptyHandsSlot : -1;
            for (int i = 0; i < kHolsterCount; ++i)
                if (name == kHolsterNames[i])
                    slot = kHolsterSlots[i];
            if (slot == -1)
            {
                if (!name.empty())
                    Log("config: unknown weapon_switch cycle entry '%s' ignored", name.c_str());
                continue;
            }
            if (std::find(s.weaponCycle.begin(), s.weaponCycle.end(), slot) == s.weaponCycle.end())
                s.weaponCycle.push_back(slot);
        }
        Log("config: triggers=%d capi_path='%ls' firing_ms=%d", s.config.triggersEnabled, s.config.triggersCapiPath.c_str(), s.config.triggerFiringMs);
        auto parseTrigger = [](const char* key, const std::string& text, TriggerEffect& effect) {
            if (!ParseTriggerEffect(text, effect))
                Log("config: triggers %s='%s' is not a trigger effect, using off", key, text.c_str());
        };
        for (int kind = 0; kind < kTriggerKindCount; ++kind)
        {
            std::string firingKey = std::string(kTriggerKindNames[kind]) + "_firing";
            parseTrigger(kTriggerKindNames[kind], s.config.triggerProfiles[kind], s.triggerProfiles[kind]);
            s.triggerHasFiring[kind] = !s.config.triggerFiring[kind].empty();
            if (s.triggerHasFiring[kind])
                parseTrigger(firingKey.c_str(), s.config.triggerFiring[kind], s.triggerFiring[kind]);
        }
        parseTrigger("empty_magazine", s.config.triggerEmpty, s.triggerEmpty);
        if (s.config.triggersEnabled)
            triggers::Start(s.config.triggersCapiPath);
        Log("config: holsters learn=%d assist=%d assist_radius_cm=%d", s.config.holsterLearn, s.config.holsterAssist, s.config.holsterAssistRadiusCm);
        s.holsterPath = s.directory + L"\\" + kHolsterFileName;
        LoadHolsters(s);
    });
}

template <typename T>
bool ResolveNext(LayerState& s, const char* name, T& function)
{
    PFN_xrVoidFunction pointer = nullptr;
    if (XR_FAILED(s.next.getInstanceProcAddr(s.instance, name, &pointer)) || !pointer)
    {
        Log("next layer is missing %s", name);
        return false;
    }
    function = reinterpret_cast<T>(pointer);
    return true;
}

XrPath ToPath(LayerState& s, const char* text)
{
    XrPath path = XR_NULL_PATH;
    if (XR_FAILED(s.next.stringToPath(s.instance, text, &path)))
    {
        Log("xrStringToPath failed for %s", text);
        return XR_NULL_PATH;
    }
    return path;
}

std::string PathText(LayerState& s, XrPath path)
{
    if (path == XR_NULL_PATH)
        return "(null)";
    char buffer[XR_MAX_PATH_LENGTH] = {};
    uint32_t length = 0;
    if (XR_FAILED(s.next.pathToString(s.instance, path, XR_MAX_PATH_LENGTH, &length, buffer)))
        return "(invalid)";
    return buffer;
}

XrAction CreateLayerAction(LayerState& s, XrActionSet actionSet, const char* name, const char* localizedName, const std::vector<XrPath>& subactionPaths)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(info.actionName, name);
    strcpy_s(info.localizedActionName, localizedName);
    info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    info.countSubactionPaths = static_cast<uint32_t>(subactionPaths.size());
    info.subactionPaths = subactionPaths.empty() ? nullptr : subactionPaths.data();
    XrAction action = XR_NULL_HANDLE;
    XrResult created = s.next.createAction(actionSet, &info, &action);
    Log("action '%s' created in '%s' (%d)", name, kGameplaySetName, created);
    return XR_SUCCEEDED(created) ? action : XR_NULL_HANDLE;
}

GripSlot* FindGrip(LayerState& s, XrPath path)
{
    auto it = std::find_if(s.grips.begin(), s.grips.end(), [path](const GripSlot& grip) { return grip.path == path; });
    return it == s.grips.end() ? nullptr : &*it;
}

void ConfigureGrips(LayerState& s, const XrActionCreateInfo& info)
{
    s.grips.clear();
    for (uint32_t i = 0; i < info.countSubactionPaths; ++i)
    {
        XrPath path = info.subactionPaths[i];
        bool left = path == s.leftHand;
        bool right = path == s.rightHand;
        if (!left && !right)
            continue;
        GripSlot grip;
        grip.path = path;
        grip.name = left ? "left" : "right";
        s.grips.push_back(grip);
    }
    for (const GripSlot& grip : s.grips)
        Log("squeeze slot %s path=%s", grip.name, PathText(s, grip.path).c_str());
}

const char* HandName(const LayerState& s, XrPath hand)
{
    return hand == s.leftHand ? "left" : hand == s.rightHand ? "right" : "unknown";
}

void TrackHeld(GripSlot& grip, float output)
{
    bool held = output > kHeldThreshold;
    if (held && !grip.held)
        grip.heldAt = std::chrono::steady_clock::now();
    grip.held = held;
}

void UpdateCommandAction(LayerState& s, XrSession session, XrAction action, ButtonState& button, const char* label, const std::string& pressCommand,
                         const std::string& releaseCommand, std::vector<std::string>& commands);
const char* ReferenceSpaceName(const LayerState& s, XrSpace space);

int HandIndex(const LayerState& s, XrPath hand)
{
    return hand == s.leftHand ? 0 : hand == s.rightHand ? 1 : -1;
}

int HolsterIndex(int slot)
{
    for (int index = 0; index < kHolsterCount; ++index)
        if (kHolsterSlots[index] == slot)
            return index;
    return -1;
}

const char* HolsterName(int slot)
{
    if (slot == kEmptyHandsSlot)
        return "empty hands";
    int index = HolsterIndex(slot);
    return index < 0 ? "unknown" : kHolsterNames[index];
}

bool Owned(const LayerState& s, int slot)
{
    return (s.holsterMask & (1 << slot)) != 0;
}

Vec3 ToLocal(const HeadFrame& head, Vec3 point)
{
    Vec3 offset = point - head.position;
    return {Dot(offset, head.right), offset.y, Dot(offset, head.forward)};
}

Vec3 ToWorld(const HeadFrame& head, Vec3 local)
{
    return head.position + head.right * local.x + Vec3{0.0f, local.y, 0.0f} + head.forward * local.z;
}

bool HandLocatable(const LayerState& s, int hand)
{
    return hand >= 0 && s.hands[hand].valid && s.head.valid && s.hands[hand].base == s.head.base;
}

void UpdateGameVars(LayerState& s)
{
    int value = 0;
    s.holsterMask = s.holsterMaskVar.ReadInt(value) ? value : 0;
    s.gunSlot = s.gunSlotVar.ReadInt(value) ? value : -1;
    int active = s.activeSlotVar.ReadInt(value) ? value : kActiveSlotUnknown;
    if (active != s.activeSlot)
        Log("active slot %d (%s)", active, active == kActiveSlotUnknown ? "unknown" : active < 0 ? "empty hands" : HolsterName(active));
    s.activeSlot = active;
    s.pickup[0] = s.pickupLeftVar.ReadInt(value) ? value : -1;
    s.pickup[1] = s.pickupRightVar.ReadInt(value) ? value : -1;
    bool multiplayer = s.multiplayerVar.ReadInt(value) && value == 1;
    if (multiplayer != s.multiplayer)
    {
        if (multiplayer)
            Log("multiplayer: button draws %s, holster assist %s", s.config.multiplayerButtonDraws ? "allowed" : "off",
                s.config.multiplayerAssist ? "allowed" : "off");
        else
            Log("singleplayer");
    }
    s.multiplayer = multiplayer;
}

// where the hand was when the grip closed, so a later gun or grenade ack can teach the holster position
void SnapshotPress(LayerState& s, GripSlot& grip)
{
    int hand = HandIndex(s, grip.path);
    grip.pressSynthetic = s.weaponSwitch.phase != SwitchPhase::Idle && s.weaponSwitch.hand == grip.path;
    grip.pressPickup = hand >= 0 && s.pickup[hand] != -1;
    grip.pressValid = HandLocatable(s, hand);
    if (grip.pressValid)
        grip.pressLocal = ToLocal(s.head, s.hands[hand].position);
}

void LearnHolster(LayerState& s, const GripSlot& grip, int slot)
{
    int index = HolsterIndex(slot);
    int hand = HandIndex(s, grip.path);
    if (!s.config.holsterLearn || index < 0 || hand < 0)
        return;
    const char* skip = grip.pressSynthetic ? "faked grab" : grip.pressPickup ? "picked up from the world" : !grip.pressValid ? "hand or head pose unavailable" : nullptr;
    if (skip)
    {
        Log("%s holster not learned: %s", kHolsterNames[index], skip);
        return;
    }
    Holster& holster = s.holsters[index][hand];
    holster.local = holster.learned ? holster.local + (grip.pressLocal - holster.local) * kHolsterLearnRate : grip.pressLocal;
    holster.learned = true;
    holster.samples = std::min(holster.samples + 1, 1000);
    Log("%s holster (%s hand) learned: grabbed at right %.0f up %.0f forward %.0f cm, now right %.0f up %.0f forward %.0f cm (%d samples)",
        kHolsterNames[index], kHandNames[hand], grip.pressLocal.x * 100.0f, grip.pressLocal.y * 100.0f, grip.pressLocal.z * 100.0f,
        holster.local.x * 100.0f, holster.local.y * 100.0f, holster.local.z * 100.0f, holster.samples);
    SaveHolster(s, index, hand);
}

void EndSwitch(LayerState& s, const char* message)
{
    WeaponSwitch& w = s.weaponSwitch;
    Log("%s %s: %s", w.assist ? "assisted grab of" : "switch to", HolsterName(w.slot), message);
    w = {};
}

// runs once per frame before the grips are sampled; sets the squeeze and pose override for this frame
void AdvanceWeaponSwitch(LayerState& s)
{
    WeaponSwitch& w = s.weaponSwitch;
    w.squeeze = -1.0f;
    w.spoof = false;
    if (w.phase == SwitchPhase::Idle)
        return;
    if (s.menuState != 0 && w.phase != SwitchPhase::Confirm)
    {
        EndSwitch(s, "cancelled by a menu");
        return;
    }
    auto now = std::chrono::steady_clock::now();
    int release = std::max(s.config.weaponSwitchReleaseFrames, 1);
    for (;;)
    {
        switch (w.phase)
        {
        case SwitchPhase::Idle:
            return;
        case SwitchPhase::Release:
        {
            // Hold grip lets go by releasing; Toggle grip lets go with one more press
            int length = w.toggle ? kTogglePulseFrames + release : release;
            if (w.frame >= length)
            {
                w.phase = SwitchPhase::WaitEmpty;
                w.frame = 0;
                continue;
            }
            w.squeeze = w.toggle && w.frame < kTogglePulseFrames ? 1.0f : 0.0f;
            ++w.frame;
            return;
        }
        case SwitchPhase::WaitEmpty:
            if (s.activeSlot == -1 || (s.activeSlot == kActiveSlotUnknown && w.frame >= release))
            {
                Log("switch: current gun holstered after %d frames", w.frame);
                if (w.slot == kEmptyHandsSlot)
                {
                    // Hold grip: a grip still squeezed stays released until you let go, so it can't grab anything
                    GripSlot* grip = FindGrip(s, w.hand);
                    if (!w.toggle && grip && grip->rawPressed)
                        grip->waitRelease = true;
                    EndSwitch(s, "done");
                    return;
                }
                w.phase = SwitchPhase::Move;
                w.frame = 0;
                continue;
            }
            if (now > w.deadline)
            {
                EndSwitch(s, "the current gun was not holstered in time, giving up");
                return;
            }
            w.squeeze = 0.0f;
            ++w.frame;
            return;
        case SwitchPhase::Move:
            if (w.frame >= std::max(s.config.weaponSwitchMoveFrames, 1))
            {
                w.phase = SwitchPhase::Grab;
                w.frame = 0;
                continue;
            }
            w.squeeze = 0.0f;
            w.spoof = true;
            ++w.frame;
            return;
        case SwitchPhase::Grab:
            if (w.frame >= std::max(s.config.weaponSwitchGrabFrames, 1))
            {
                GripSlot* grip = FindGrip(s, w.hand);
                if (!w.assist && !w.toggle && grip && !grip->rawPressed)
                {
                    grip->forceHeld = true;
                    Log("switch: %s hand keeps the gun until you squeeze grip", grip->name);
                }
                w.phase = SwitchPhase::Confirm;
                w.frame = 0;
                w.deadline = now + std::chrono::milliseconds(std::max(s.config.weaponSwitchTimeoutMs, 1));
                continue;
            }
            w.squeeze = 1.0f;
            w.spoof = true;
            ++w.frame;
            return;
        case SwitchPhase::Confirm:
            if (s.gunAck != w.startGunAck || s.grenadeAck != w.startGrenadeAck)
            {
                EndSwitch(s, "confirmed by the game");
                return;
            }
            if (now > w.deadline)
            {
                EndSwitch(s, "no gun or grenade ack from the game, the holster position may be off");
                return;
            }
            ++w.frame;
            return;
        }
    }
}

void BeginGrab(LayerState& s, bool assist, XrPath hand, int slot, Vec3 target, bool holding)
{
    WeaponSwitch& w = s.weaponSwitch;
    int toggle = 0;
    if (!s.gunGripToggleVar.ReadInt(toggle))
        toggle = 0;
    w = {};
    w.assist = assist;
    w.toggle = toggle == 1;
    w.hand = hand;
    w.slot = slot;
    w.target = target;
    w.phase = holding || slot == kEmptyHandsSlot ? SwitchPhase::Release : SwitchPhase::Move;
    w.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(s.config.weaponSwitchTimeoutMs, 1));
    w.startGunAck = s.gunAck;
    w.startGrenadeAck = s.grenadeAck;
    for (GripSlot& grip : s.grips)
        grip.forceHeld = false;
}

// the next entry of [weapon_switch] cycle after what's in hand, skipping guns you don't carry and grenades you don't have
int NextGunSlot(const LayerState& s)
{
    const std::vector<int>& cycle = s.weaponCycle;
    int current = s.activeSlot >= 0 ? s.activeSlot : kEmptyHandsSlot;
    int count = static_cast<int>(cycle.size());
    int position = -1;
    for (int i = 0; i < count; ++i)
        if (cycle[i] == current)
            position = i;
    if (position < 0 && current == kEmptyHandsSlot && s.gunSlot >= 0 && Owned(s, s.gunSlot))
        return s.gunSlot;
    for (int step = 1; step <= count; ++step)
    {
        int slot = cycle[(position + step + count) % count];
        if (slot == current)
            continue;
        if (slot == kEmptyHandsSlot ? s.activeSlot >= 0 : Owned(s, slot))
            return slot;
    }
    return -1;
}

void StartWeaponSwitch(LayerState& s, int slot, std::vector<std::string>& commands)
{
    if (slot == kEmptyHandsSlot)
    {
        BeginGrab(s, false, s.gunHand, slot, {}, true);
        Log("switch to empty hands: holstering the gun in the %s hand (%s grip)", HandName(s, s.gunHand), s.weaponSwitch.toggle ? "toggle" : "hold");
        return;
    }
    int hand = HandIndex(s, s.gunHand);
    int index = HolsterIndex(slot);
    const Holster* holster = hand >= 0 && index >= 0 ? &s.holsters[index][hand] : nullptr;
    bool learned = holster && holster->learned;
    if (s.config.weaponSwitchMethod == "command" || !learned)
    {
        if (!learned && s.config.weaponSwitchMethod != "command")
            Log("switch to %s: holster not learned for the %s hand yet, draw it once by hand; trying the console command instead", HolsterName(slot),
                HandName(s, s.gunHand));
        ++s.commandTicket;
        commands.push_back("tf2vr_draw " + std::to_string(s.commandTicket) + " " + std::to_string(slot));
        return;
    }
    const GripSlot* grip = FindGrip(s, s.gunHand);
    bool holding = s.activeSlot >= 0 || (s.activeSlot == kActiveSlotUnknown && grip && grip->held);
    BeginGrab(s, false, s.gunHand, slot, holster->local, holding);
    Log("switch to %s with the %s hand (%s grip)%s", HolsterName(slot), HandName(s, s.gunHand), s.weaponSwitch.toggle ? "toggle" : "hold",
        holding ? ", holstering the current gun first" : "");
}

// adaptive trigger of the gun hand: a profile per kind of gun, another while firing, loose on an empty magazine
void UpdateTriggers(LayerState& s)
{
    if (!s.config.triggersEnabled)
        return;
    int value = 0;
    int kind = s.triggerKindVar.ReadInt(value) && value >= 0 && value < kTriggerKindCount ? value : 0;
    int clip = s.clipVar.ReadInt(value) ? value : -1;
    auto now = std::chrono::steady_clock::now();
    if (kind != s.triggerKind)
        Log("trigger kind %d (%s)", kind, kTriggerKindNames[kind]);
    else if (clip >= 0 && s.clip >= 0 && clip < s.clip)
        s.firingUntil = now + std::chrono::milliseconds(std::max(s.config.triggerFiringMs, 0));
    s.triggerKind = kind;
    s.clip = clip;

    TriggerEffect effect = s.triggerProfiles[kind];
    if (s.menuState != 0 || s.reloadWaiting)
        effect = {};
    else if (kind != 0 && clip == 0)
        effect = s.triggerEmpty;
    else if (s.triggerHasFiring[kind] && now < s.firingUntil)
        effect = s.triggerFiring[kind];
    int gun = HandIndex(s, s.gunHand);
    for (int hand = 0; hand < 2; ++hand)
        triggers::SetTrigger(hand, hand == gun ? effect : TriggerEffect{});
}

void UpdateWeaponButtons(LayerState& s, XrSession session, std::vector<std::string>& commands)
{
    std::vector<std::string> none;
    UpdateCommandAction(s, session, s.nextWeapon, s.nextWeaponButton, "next weapon", {}, {}, none);
    UpdateCommandAction(s, session, s.drawPrimary, s.drawPrimaryButton, "draw primary", {}, {}, none);
    UpdateCommandAction(s, session, s.drawSecondary, s.drawSecondaryButton, "draw secondary", {}, {}, none);
    auto pressed = [](const ButtonState& button) { return button.changed && button.pressed; };
    int slot = -2;
    if (pressed(s.nextWeaponButton))
        slot = NextGunSlot(s);
    else if (pressed(s.drawPrimaryButton))
        slot = 0;
    else if (pressed(s.drawSecondaryButton))
        slot = 1;
    if (slot == -2)
        return;

    const char* blocked = s.menuState != 0 ? "menu open" : s.weaponSwitch.phase != SwitchPhase::Idle ? "a switch is already running"
                        : s.reloadWaiting                                         ? "reloading"
                        : s.multiplayer && !s.config.multiplayerButtonDraws && slot != kEmptyHandsSlot ? "off in multiplayer ([multiplayer] button_draws)"
                        : slot == -1                                              ? "no other gun to switch to"
                        : slot == kEmptyHandsSlot && s.activeSlot == -1           ? "hands already empty"
                        : slot >= 0 && !Owned(s, slot)                            ? "you don't carry that"
                        : slot == s.activeSlot                                    ? "already in hand"
                                                                                  : nullptr;
    if (blocked)
    {
        Log("weapon switch ignored: %s (holster mask 0x%x, active slot %d)", blocked, s.holsterMask, s.activeSlot);
        return;
    }
    StartWeaponSwitch(s, slot, commands);
}

// a grip press near a learned holster is moved into it, which makes every holster bigger
void TryStartAssist(LayerState& s, const GripSlot& grip)
{
    int hand = HandIndex(s, grip.path);
    if (!s.config.holsterAssist || (s.multiplayer && !s.config.multiplayerAssist) || s.weaponSwitch.phase != SwitchPhase::Idle || s.menuState != 0 || s.reloadWaiting || !HandLocatable(s, hand))
        return;
    if (s.pickup[hand] != -1 || (grip.path == s.gunHand && s.activeSlot >= 0))
        return;
    Vec3 local = ToLocal(s.head, s.hands[hand].position);
    int best = -1;
    float bestDistance = 0.0f;
    for (int index = 0; index < kHolsterCount; ++index)
    {
        int slot = kHolsterSlots[index];
        const Holster& holster = s.holsters[index][hand];
        if (!holster.learned || !Owned(s, slot) || slot == s.activeSlot)
            continue;
        float distance = Length(local - holster.local);
        if (best < 0 || distance < bestDistance)
        {
            best = index;
            bestDistance = distance;
        }
    }
    if (best < 0 || bestDistance * 100.0f > static_cast<float>(s.config.holsterAssistRadiusCm))
        return;
    BeginGrab(s, true, grip.path, kHolsterSlots[best], s.holsters[best][hand].local, false);
    Log("assisted grab: %s hand %.0f cm from the %s holster", grip.name, bestDistance * 100.0f, kHolsterNames[best]);
    AdvanceWeaponSwitch(s);
}

void SpoofHandPose(LayerState& s, XrSpace space, XrSpace baseSpace, XrSpaceLocationFlags flags, XrPosef& pose)
{
    const WeaponSwitch& w = s.weaponSwitch;
    if (!w.spoof || !(flags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        return;
    auto it = std::find_if(s.handSpaces.begin(), s.handSpaces.end(), [space](const HandSpace& h) { return h.space == space; });
    if (it == s.handSpaces.end() || it->hand != w.hand)
        return;
    int hand = HandIndex(s, w.hand);
    if (!HandLocatable(s, hand) || baseSpace != s.head.base)
    {
        if (!s.loggedSpoofBase)
        {
            s.loggedSpoofBase = true;
            Log("cannot move the %s hand: hand located relative to %s space, head relative to %s", HandName(s, w.hand), ReferenceSpaceName(s, baseSpace),
                ReferenceSpaceName(s, s.head.base));
        }
        return;
    }
    Vec3 delta = ToWorld(s.head, w.target) - s.hands[hand].position;
    if (!w.loggedSpoof)
    {
        s.weaponSwitch.loggedSpoof = true;
        Log("%s hand moved %.0f cm to the %s holster", HandName(s, w.hand), Length(delta) * 100.0f, HolsterName(w.slot));
    }
    pose.position.x += delta.x;
    pose.position.y += delta.y;
    pose.position.z += delta.z;
}

void UpdateReloadState(LayerState& s)
{
    int highlight = 0;
    if (!s.reloadHighlightVar.ReadInt(highlight))
        highlight = 0;
    if (highlight != s.reloadHighlight)
    {
        Log("reload highlight %d", highlight);
        s.reloadHighlight = highlight;
    }
    auto now = std::chrono::steady_clock::now();
    if (highlight != 0)
        s.reloadSeenAt = now;
    bool waiting = highlight != 0 || (s.reloadWaiting && now - s.reloadSeenAt < kReloadEndGrace);
    if (waiting == s.reloadWaiting)
        return;
    Log("reload %s", waiting ? "started" : "finished");
    s.reloadWaiting = waiting;
}

void UpdateSqueeze(LayerState& s, XrSession session)
{
    for (GripSlot& grip : s.grips)
    {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = s.squeeze;
        get.subactionPath = grip.path;
        XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
        bool squeezeActive = XR_SUCCEEDED(s.next.getActionStateFloat(session, &get, &squeeze)) && squeeze.isActive;
        float raw = squeezeActive ? squeeze.currentState : 0.0f;

        bool grabbing = false;
        XrTime grabTime = 0;
        grip.grabBound = false;
        if (s.reloadGrab != XR_NULL_HANDLE)
        {
            get.action = s.reloadGrab;
            XrActionStateBoolean grab{XR_TYPE_ACTION_STATE_BOOLEAN};
            grip.grabBound = XR_SUCCEEDED(s.next.getActionStateBoolean(session, &get, &grab)) && grab.isActive;
            grabbing = grip.grabBound && grab.currentState;
            grabTime = grab.lastChangeTime;
        }

        auto now = std::chrono::steady_clock::now();
        bool rawPressed = raw > kHeldThreshold;
        bool rawEdge = rawPressed && !grip.rawPressed;
        if (rawEdge)
            grip.pressAt = now;
        grip.rawPressed = rawPressed;
        if (rawEdge)
            TryStartAssist(s, grip);

        bool routed = grip.grabBound && s.reloadWaiting && grip.path != s.gunHand && !grip.grenadeHeld;
        if (routed != grip.routed)
            Log("%s hand: %s", grip.name, routed ? "grip blocked, reload grab active" : "grip restored");
        grip.routed = routed;

        float output = raw;
        if (grip.grenadeHeld)
        {
            output = rawEdge ? 0.0f : 1.0f;
            if (rawEdge)
            {
                grip.grenadeHeld = false;
                grip.waitRelease = true;
                Log("%s hand: grenade let go", grip.name);
            }
        }
        else if (grip.waitRelease)
        {
            output = 0.0f;
            if (!rawPressed)
                grip.waitRelease = false;
        }
        else if (routed)
        {
            output = grabbing ? 1.0f : 0.0f;
        }
        else if (s.grenadeToggleActive && !rawPressed && grip.output > kHeldThreshold && now - grip.pressAt < kGrenadeMinPress)
        {
            output = grip.output;
        }

        const WeaponSwitch& w = s.weaponSwitch;
        bool faked = w.phase != SwitchPhase::Idle && w.hand == grip.path && w.squeeze >= 0.0f;
        if (faked)
        {
            output = w.squeeze;
        }
        else if (grip.forceHeld)
        {
            if (rawEdge || s.menuState != 0)
            {
                grip.forceHeld = false;
                Log("%s hand: grip back to normal", grip.name);
            }
            else
            {
                output = 1.0f;
            }
        }
        grip.changed = output != grip.output;
        grip.sampled = routed || grip.changed || output != raw;
        if (grip.changed)
            grip.changeTime = routed ? grabTime : faked || grip.forceHeld ? std::max(squeeze.lastChangeTime, s.head.time) : squeeze.lastChangeTime;
        grip.output = output;
        bool wasHeld = grip.held;
        TrackHeld(grip, output);
        if (grip.held && !wasHeld)
            SnapshotPress(s, grip);
    }
}

const GripSlot* RecentGrab(const LayerState& s)
{
    auto now = std::chrono::steady_clock::now();
    const GripSlot* newest = nullptr;
    for (const GripSlot& grip : s.grips)
        if (now - grip.heldAt <= kGunGrabWindow && (!newest || grip.heldAt > newest->heldAt))
            newest = &grip;
    return newest;
}

void UpdateGrenade(LayerState& s)
{
    int toggle = 0;
    if (!s.gunGripToggleVar.ReadInt(toggle))
        toggle = 0;
    bool active = s.config.grenadeToggle && toggle == 1;
    if (active != s.grenadeToggleActive)
        Log("grenade toggle %s", active ? "on (Gun Grip is Toggle)" : "off");
    s.grenadeToggleActive = active;

    if (!active || s.menuState != 0)
    {
        for (GripSlot& grip : s.grips)
        {
            if (grip.grenadeHeld)
                Log("%s hand: grenade toggle cleared", grip.name);
            grip.grenadeHeld = false;
        }
    }

    int ack = 0;
    if (!s.grenadeAckVar.ReadInt(ack))
        return;
    if (!s.grenadeAckKnown)
    {
        s.grenadeAckKnown = true;
        s.grenadeAck = ack;
        return;
    }
    if (ack == s.grenadeAck)
        return;
    s.grenadeAck = ack;
    if (ack > 0)
    {
        if (const GripSlot* grabbed = RecentGrab(s))
            LearnHolster(s, *grabbed, kGrenadeSlot);
    }
    if (ack <= 0 || !active)
        return;

    const GripSlot* recent = RecentGrab(s);
    if (!recent)
    {
        Log("grenade ack %d: no recent grab", ack);
        return;
    }
    GripSlot& grip = *std::find_if(s.grips.begin(), s.grips.end(), [recent](const GripSlot& g) { return &g == recent; });
    if (grip.output <= kHeldThreshold)
    {
        Log("grenade ack %d: %s hand already let go", ack, grip.name);
        return;
    }
    grip.grenadeHeld = true;
    Log("grenade ack %d: %s hand holds the grenade until the next grip press", ack, grip.name);
}

void UpdateGunHand(LayerState& s)
{
    int ack = 0;
    if (!s.gunAckVar.ReadInt(ack))
        return;
    if (!s.gunAckKnown)
    {
        s.gunAckKnown = true;
        s.gunAck = ack;
        return;
    }
    if (ack == s.gunAck)
        return;
    s.gunAck = ack;
    if (ack <= 0)
        return;

    const GripSlot* newest = RecentGrab(s);
    if (!newest)
    {
        Log("gun ack %d: no recent grab, gun hand stays %s", ack, HandName(s, s.gunHand));
        return;
    }
    s.gunHand = newest->path;
    Log("gun ack %d: gun hand %s, slot %d", ack, newest->name, s.gunSlot);
    LearnHolster(s, *newest, s.gunSlot);
}

void UpdateCommandAction(LayerState& s, XrSession session, XrAction action, ButtonState& button, const char* label, const std::string& pressCommand,
                         const std::string& releaseCommand, std::vector<std::string>& commands)
{
    button.changed = false;
    if (action == XR_NULL_HANDLE)
        return;
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(s.next.getActionStateBoolean(session, &get, &state)))
        return;

    button.active = state.isActive == XR_TRUE;
    bool pressed = state.isActive && state.currentState == XR_TRUE;
    if (pressed == button.pressed)
        return;
    button.pressed = pressed;
    button.changed = true;
    button.changeTime = state.lastChangeTime;
    Log("%s %s", label, pressed ? "pressed" : "released");
    const std::string& command = pressed ? pressCommand : releaseCommand;
    if (!command.empty())
        commands.push_back(command);
}

void UpdateCrouch(LayerState& s, XrSession session, std::vector<std::string>& commands)
{
    ButtonState& button = s.crouchButton;
    CrouchState& c = s.crouchState;
    button.changed = false;
    if (s.crouch == XR_NULL_HANDLE)
        return;
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = s.crouch;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(s.next.getActionStateBoolean(session, &get, &state)))
        return;
    button.active = state.isActive == XR_TRUE;
    bool pressed = state.isActive && state.currentState == XR_TRUE;
    if (pressed != button.pressed)
    {
        button.pressed = pressed;
        button.changed = true;
        button.changeTime = state.lastChangeTime;
        Log("crouch %s", pressed ? "pressed" : "released");
    }

    bool jumpEdge = false;
    bool jumpPressed = false;
    if (s.jump != XR_NULL_HANDLE && s.jumpHand != XR_NULL_PATH)
    {
        get.action = s.jump;
        get.subactionPath = s.jumpHand;
        XrActionStateBoolean jump{XR_TYPE_ACTION_STATE_BOOLEAN};
        jumpPressed = XR_SUCCEEDED(s.next.getActionStateBoolean(session, &get, &jump)) && jump.isActive && jump.currentState;
        jumpEdge = jumpPressed && !c.jumpPressed;
        c.jumpPressed = jumpPressed;
    }

    bool buttonMode = s.config.crouchMode == "button";
    if (buttonMode && !pressed)
        c.released = false;
    bool ducked = buttonMode ? pressed && !c.released : pressed != c.inverted;
    if (jumpEdge && ducked && s.config.crouchUncrouchOnJump)
    {
        if (buttonMode)
            c.released = true;
        else
            c.inverted = pressed;
        ducked = false;
        c.jumpPending = true;
        c.jumpReleaseAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(s.config.crouchJumpDelayMs, 0));
        Log("crouch released by jump, jump held back %d ms", s.config.crouchJumpDelayMs);
    }
    if (c.jumpPending && std::chrono::steady_clock::now() >= c.jumpReleaseAt)
    {
        c.jumpPending = false;
        if (!jumpPressed)
            c.jumpPulseEnd = s.frame + kJumpPulseFrames;
        Log("jump passed on%s", jumpPressed ? "" : " as a tap");
    }
    bool pulsing = s.frame < c.jumpPulseEnd;
    bool jumpOutput = c.jumpPending ? false : jumpPressed || pulsing;
    c.jumpChanged = jumpOutput != c.jumpOutput;
    c.jumpOutput = jumpOutput;
    c.jumpOverride = c.jumpPending || pulsing;
    if (ducked == c.ducked)
        return;
    c.ducked = ducked;
    const std::string& command = ducked ? s.config.crouchPressCommand : s.config.crouchReleaseCommand;
    if (!command.empty())
        commands.push_back(command);
}

void UpdateMenuState(LayerState& s)
{
    int menu = -1;
    if (!s.menuActiveVar.ReadInt(menu))
        menu = -1;
    if (menu != s.menuState)
    {
        Log("menu state: %s", menu < 0 ? "unknown (companion script not running)" : menu ? "menu or start screen" : "gameplay");
        s.menuState = menu;
    }
    int phase = -1;
    if (!s.startupPhaseVar.ReadInt(phase))
        phase = -1;
    if (phase != s.startupPhase)
    {
        Log("startup phase: %d", phase);
        s.startupPhase = phase;
    }
}

void UpdateTacticalHand(LayerState& s)
{
    if (s.config.tacticalHand != "auto")
        return;
    XrPath support = s.gunHand == s.leftHand ? s.rightHand : s.leftHand;
    if (support == s.tacticalHand)
        return;
    s.tacticalHand = support;
    s.triggerOverride.applies = false;
    s.triggerOverride.blocked = false;
    Log("tactical hand %s (support hand)", HandName(s, support));
}

void UpdateTriggerOverride(LayerState& s, XrSession session)
{
    TriggerOverride& o = s.triggerOverride;
    o.changed = false;
    if (s.trigger == XR_NULL_HANDLE || s.tacticalHand == XR_NULL_PATH)
    {
        o.applies = false;
        return;
    }

    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = s.trigger;
    get.subactionPath = s.tacticalHand;
    XrActionStateFloat raw{XR_TYPE_ACTION_STATE_FLOAT};
    bool rawActive = XR_SUCCEEDED(s.next.getActionStateFloat(session, &get, &raw)) && raw.isActive;
    bool rawPressed = rawActive && raw.currentState > kHeldThreshold;

    const GripSlot* support = FindGrip(s, s.tacticalHand);
    bool reloadBlock = s.menuState == 0 && s.reloadWaiting && support && support->grabBound;
    if (o.reloadBlocked && !reloadBlock && !s.reloadWaiting && rawPressed && !o.waitRelease)
    {
        o.waitRelease = true;
        Log("%s trigger held back until released", HandName(s, s.tacticalHand));
    }
    if (o.waitRelease && !rawPressed)
        o.waitRelease = false;
    if (reloadBlock != o.reloadBlocked)
        Log("%s trigger %s", HandName(s, s.tacticalHand), reloadBlock ? "blocked (reload)" : "no longer blocked by the reload");
    o.reloadBlocked = reloadBlock;

    bool tacticalBound = s.tacticalButton.active;
    if (!tacticalBound && !reloadBlock && !o.waitRelease)
    {
        o.applies = false;
        return;
    }

    if (tacticalBound)
    {
        bool tacticalBlock = s.config.tacticalBlockHandTrigger && s.menuState == 0;
        if (tacticalBlock != o.blocked)
            Log("%s trigger %s", HandName(s, s.tacticalHand), tacticalBlock ? "blocked (gameplay, tactical bound)" : "passes through");
        o.blocked = tacticalBlock;
    }
    bool blocked = (tacticalBound && o.blocked) || reloadBlock || o.waitRelease;

    float output = std::max(s.tacticalButton.pressed ? 1.0f : 0.0f, rawActive && !blocked ? raw.currentState : 0.0f);
    o.changed = !o.applies || output != o.output;
    if (o.changed)
        o.changeTime = s.tacticalButton.changed || !rawActive ? s.tacticalButton.changeTime : raw.lastChangeTime;
    if (s.tacticalButton.changed)
        Log("tactical drives %s trigger = %.1f", HandName(s, s.tacticalHand), output);
    o.output = output;
    o.applies = true;
}

float PitchDegrees(const XrQuaternionf& q)
{
    float forwardUp = 2.0f * (q.w * q.x - q.y * q.z);
    return std::asin(std::clamp(forwardUp, -1.0f, 1.0f)) * 57.29578f;
}

const char* ReferenceSpaceName(const LayerState& s, XrSpace space)
{
    for (const auto& [handle, type] : s.referenceSpaces)
    {
        if (handle != space)
            continue;
        switch (type)
        {
        case XR_REFERENCE_SPACE_TYPE_VIEW: return "view";
        case XR_REFERENCE_SPACE_TYPE_LOCAL: return "local";
        case XR_REFERENCE_SPACE_TYPE_STAGE: return "stage";
        default: return "other reference";
        }
    }
    return "non-reference";
}

void RecordAimPose(LayerState& s, XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocationFlags flags, const XrPosef& pose)
{
    auto it = std::find_if(s.aimSpaces.begin(), s.aimSpaces.end(), [space](const AimSpace& aim) { return aim.space == space; });
    if (it == s.aimSpaces.end())
        return;
    int hand = HandIndex(s, it->hand);
    if (hand >= 0 && (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && time >= s.hands[hand].time)
        s.hands[hand] = {true, FromXr(pose.position), baseSpace, time};
    if (!s.loggedAimLocate)
    {
        s.loggedAimLocate = true;
        Log("app locates aim %s relative to %s space", HandName(s, it->hand), ReferenceSpaceName(s, baseSpace));
    }
    XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if (it->hand != s.gunHand || (flags & needed) != needed || time < s.gunPoseTime)
        return;
    s.gunPitch = PitchDegrees(pose.orientation);
    s.gunHeight = pose.position.y;
    s.gunPoseBase = baseSpace;
    s.gunPoseTime = time;
    s.gunPoseValid = true;
}

void RecordHead(LayerState& s, XrSpace baseSpace, XrTime time, const XrView* views, uint32_t count)
{
    if (count == 0 || time < s.headTime)
        return;
    float sum = 0.0f;
    for (uint32_t i = 0; i < count; ++i)
        sum += views[i].pose.position.y;
    s.headHeight = sum / static_cast<float>(count);
    s.headBase = baseSpace;
    s.headTime = time;
    s.headValid = true;

    Vec3 position;
    for (uint32_t i = 0; i < count; ++i)
        position = position + FromXr(views[i].pose.position);
    s.head.position = position * (1.0f / static_cast<float>(count));
    Vec3 forward = Rotate(views[0].pose.orientation, {0.0f, 0.0f, -1.0f});
    forward.y = 0.0f;
    float length = Length(forward);
    // looking straight up or down keeps the last heading
    if (length > 0.1f)
    {
        s.head.forward = forward * (1.0f / length);
        s.head.right = {-s.head.forward.z, 0.0f, s.head.forward.x};
    }
    s.head.base = baseSpace;
    s.head.time = time;
    s.head.valid = true;
}

void UpdateSprint(LayerState& s, XrSession session)
{
    SprintState& sprint = s.sprint;
    sprint.changed = false;
    if (s.stickClick == XR_NULL_HANDLE || s.sprintHand == XR_NULL_PATH)
        return;

    bool sameBase = s.gunPoseBase == s.headBase;
    if (s.gunPoseValid && s.headValid && !sameBase && !s.loggedBaseMismatch)
    {
        s.loggedBaseMismatch = true;
        Log("gun and head are located in different spaces (%s, %s), sprint disabled", ReferenceSpaceName(s, s.gunPoseBase), ReferenceSpaceName(s, s.headBase));
    }
    bool measured = s.gunPoseValid && s.headValid && sameBase;
    float belowEyes = measured ? (s.headHeight - s.gunHeight) * 100.0f : 0.0f;
    bool lowered = sprint.lowered;
    if (!measured)
        lowered = false;
    else if (!lowered && belowEyes > static_cast<float>(s.config.sprintStartBelowEyesCm))
        lowered = true;
    else if (lowered && belowEyes < static_cast<float>(s.config.sprintStopBelowEyesCm))
        lowered = false;
    if (lowered != sprint.lowered)
        Log("gun %s: %.0f cm below eyes, pitch %.0f degrees", lowered ? "lowered" : "raised", belowEyes, s.gunPitch);
    bool stopMethod = s.config.sprintStopMethod == "stick";
    if (lowered)
    {
        sprint.stopStart = 0;
        sprint.stopEnd = 0;
    }
    else if (sprint.lowered && measured && stopMethod && s.menuState == 0 && !s.reloadWaiting)
    {
        sprint.stopStart = s.frame + kSprintStopGapFrames;
        sprint.stopEnd = sprint.stopStart + static_cast<uint64_t>(std::max(s.config.sprintStopFrames, 1));
        Log("sprint stop by %s", s.config.sprintStopMethod.c_str());
    }
    sprint.lowered = lowered;
    bool stopping = sprint.stopEnd != 0 && s.frame >= sprint.stopStart && s.frame < sprint.stopEnd;
    if (sprint.stopEnd != 0 && s.frame >= sprint.stopEnd)
    {
        sprint.stopStart = 0;
        sprint.stopEnd = 0;
    }
    if (measured && s.frame >= s.gunPoseLoggedFrame + kPitchLogFrames)
    {
        s.gunPoseLoggedFrame = s.frame;
        Log("gun %.0f cm below eyes, pitch %.0f degrees (%s hand)", belowEyes, s.gunPitch, HandName(s, s.gunHand));
    }

    bool wanted = lowered && s.menuState == 0 && !s.reloadWaiting;
    bool pressed = false;
    if (s.config.sprintMode == "pulse")
    {
        if (!wanted)
            sprint.pulseEnd = 0;
        else if (sprint.pulseEnd == 0)
            sprint.pulseEnd = s.frame + kSprintPulseFrames;
        pressed = sprint.pulseEnd != 0 && s.frame < sprint.pulseEnd;
    }
    else
    {
        pressed = wanted;
    }
    bool stickZeroed = stopping && s.config.sprintStopMethod == "stick";
    sprint.stickChanged = stickZeroed != sprint.stickZeroed;
    sprint.stickZeroed = stickZeroed;
    if (pressed != sprint.pressed)
        Log("sprint %s", pressed ? "pressed" : "released");
    sprint.pressed = pressed;

    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = s.stickClick;
    get.subactionPath = s.sprintHand;
    XrActionStateBoolean raw{XR_TYPE_ACTION_STATE_BOOLEAN};
    bool rawPressed = XR_SUCCEEDED(s.next.getActionStateBoolean(session, &get, &raw)) && raw.isActive && raw.currentState;
    bool output = rawPressed || pressed;
    sprint.changed = output != sprint.output;
    if (sprint.changed)
        sprint.changeTime = rawPressed != sprint.output && raw.lastChangeTime != 0 ? raw.lastChangeTime : s.gunPoseTime;
    sprint.output = output;
}

XrResult XRAPI_CALL LayerGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

XrResult XRAPI_CALL HookDestroyInstance(XrInstance instance)
{
    LayerState& s = State();
    XrResult result = s.next.destroyInstance(instance);
    std::lock_guard lock(s.mutex);
    for (int hand = 0; hand < 2; ++hand)
        triggers::SetTrigger(hand, {});
    s.triggerKind = 0;
    s.clip = -1;
    s.squeeze = XR_NULL_HANDLE;
    s.trigger = XR_NULL_HANDLE;
    s.reloadGrab = XR_NULL_HANDLE;
    s.crouch = XR_NULL_HANDLE;
    s.tactical = XR_NULL_HANDLE;
    s.grips.clear();
    s.crouchButton = {};
    s.tacticalButton = {};
    s.triggerOverride = {};
    s.aim = XR_NULL_HANDLE;
    s.stickClick = XR_NULL_HANDLE;
    s.stick = XR_NULL_HANDLE;
    s.jump = XR_NULL_HANDLE;
    s.crouchState = {};
    s.aimSpaces.clear();
    s.referenceSpaces.clear();
    s.gunPoseValid = false;
    s.gunPoseTime = 0;
    s.headValid = false;
    s.headTime = 0;
    s.sprint = {};
    s.poseActions.clear();
    s.handSpaces.clear();
    s.hands[0] = {};
    s.hands[1] = {};
    s.head = {};
    s.nextWeapon = XR_NULL_HANDLE;
    s.detonate = XR_NULL_HANDLE;
    s.drawPrimary = XR_NULL_HANDLE;
    s.drawSecondary = XR_NULL_HANDLE;
    s.nextWeaponButton = {};
    s.drawPrimaryButton = {};
    s.drawSecondaryButton = {};
    s.weaponSwitch = {};
    s.instance = XR_NULL_HANDLE;
    Log("instance destroyed (%d)", result);
    return result;
}

XrResult XRAPI_CALL HookCreateActionSet(XrInstance instance, const XrActionSetCreateInfo* createInfo, XrActionSet* actionSet)
{
    LayerState& s = State();
    XrResult result = s.next.createActionSet(instance, createInfo, actionSet);
    if (XR_FAILED(result) || !createInfo)
        return result;

    Log("action set '%s' created", createInfo->actionSetName);
    if (std::strcmp(createInfo->actionSetName, kGameplaySetName) != 0)
        return result;

    XrAction tactical = s.config.tacticalEnabled ? CreateLayerAction(s, *actionSet, kTacticalActionName, kTacticalLocalizedName, {}) : XR_NULL_HANDLE;
    XrAction crouch = s.config.crouchEnabled ? CreateLayerAction(s, *actionSet, kCrouchActionName, kCrouchLocalizedName, {}) : XR_NULL_HANDLE;
    XrAction reloadGrab =
        s.config.reloadGrabEnabled ? CreateLayerAction(s, *actionSet, kReloadGrabActionName, kReloadGrabLocalizedName, {s.leftHand, s.rightHand}) : XR_NULL_HANDLE;
    bool weapons = s.config.weaponSwitchEnabled;
    XrAction nextWeapon = weapons ? CreateLayerAction(s, *actionSet, kNextWeaponActionName, kNextWeaponLocalizedName, {}) : XR_NULL_HANDLE;
    XrAction drawPrimary = weapons ? CreateLayerAction(s, *actionSet, kDrawPrimaryActionName, kDrawPrimaryLocalizedName, {}) : XR_NULL_HANDLE;
    XrAction drawSecondary = weapons ? CreateLayerAction(s, *actionSet, kDrawSecondaryActionName, kDrawSecondaryLocalizedName, {}) : XR_NULL_HANDLE;
    XrAction detonate = s.config.detonateEnabled ? CreateLayerAction(s, *actionSet, kDetonateActionName, kDetonateLocalizedName, {}) : XR_NULL_HANDLE;

    std::lock_guard lock(s.mutex);
    s.tactical = tactical;
    s.crouch = crouch;
    s.reloadGrab = reloadGrab;
    s.nextWeapon = nextWeapon;
    s.drawPrimary = drawPrimary;
    s.drawSecondary = drawSecondary;
    s.detonate = detonate;
    s.detonateButton = {};
    s.tacticalButton = {};
    s.crouchButton = {};
    s.nextWeaponButton = {};
    s.drawPrimaryButton = {};
    s.drawSecondaryButton = {};
    return result;
}

XrResult XRAPI_CALL HookCreateAction(XrActionSet actionSet, const XrActionCreateInfo* createInfo, XrAction* action)
{
    LayerState& s = State();
    XrResult result = s.next.createAction(actionSet, createInfo, action);
    if (XR_FAILED(result) || !createInfo)
        return result;

    Log("action '%s' type %d subaction paths %u", createInfo->actionName, createInfo->actionType, createInfo->countSubactionPaths);
    std::lock_guard lock(s.mutex);
    if (createInfo->actionType == XR_ACTION_TYPE_POSE_INPUT)
        s.poseActions.push_back(*action);
    if (createInfo->actionType == XR_ACTION_TYPE_POSE_INPUT && std::strcmp(createInfo->actionName, kAimActionName) == 0)
        s.aim = *action;
    else if (createInfo->actionType == XR_ACTION_TYPE_BOOLEAN_INPUT && std::strcmp(createInfo->actionName, kJumpActionName) == 0)
        s.jump = *action;
    else if (createInfo->actionType == XR_ACTION_TYPE_VECTOR2F_INPUT && std::strcmp(createInfo->actionName, kStickActionName) == 0 && s.config.sprintEnabled)
        s.stick = *action;
    else if (createInfo->actionType == XR_ACTION_TYPE_BOOLEAN_INPUT && std::strcmp(createInfo->actionName, kStickClickActionName) == 0 && s.config.sprintEnabled)
        s.stickClick = *action;
    if (createInfo->actionType != XR_ACTION_TYPE_FLOAT_INPUT)
        return result;

    if (std::strcmp(createInfo->actionName, kTriggerActionName) == 0)
    {
        s.trigger = *action;
    }
    else if (std::strcmp(createInfo->actionName, kSqueezeActionName) == 0)
    {
        s.squeeze = *action;
        ConfigureGrips(s, *createInfo);
    }
    return result;
}

XrResult XRAPI_CALL HookCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space)
{
    LayerState& s = State();
    XrResult result = s.next.createReferenceSpace(session, createInfo, space);
    if (XR_FAILED(result) || !createInfo || !space)
        return result;
    std::lock_guard lock(s.mutex);
    s.referenceSpaces.emplace_back(*space, createInfo->referenceSpaceType);
    return result;
}

XrResult XRAPI_CALL HookCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* createInfo, XrSpace* space)
{
    LayerState& s = State();
    XrResult result = s.next.createActionSpace(session, createInfo, space);
    if (XR_FAILED(result) || !createInfo || !space)
        return result;
    std::lock_guard lock(s.mutex);
    bool pose = std::find(s.poseActions.begin(), s.poseActions.end(), createInfo->action) != s.poseActions.end();
    if (pose && HandIndex(s, createInfo->subactionPath) >= 0)
    {
        s.handSpaces.push_back({*space, createInfo->subactionPath});
        Log("pose space created for %s hand", HandName(s, createInfo->subactionPath));
    }
    else if (pose)
    {
        Log("pose space created without a hand path (%s), it will not follow faked grabs", PathText(s, createInfo->subactionPath).c_str());
    }
    if (s.aim == XR_NULL_HANDLE || createInfo->action != s.aim)
        return result;
    s.aimSpaces.push_back({*space, createInfo->subactionPath});
    Log("aim space created for %s hand", HandName(s, createInfo->subactionPath));
    return result;
}

XrResult XRAPI_CALL HookLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location)
{
    LayerState& s = State();
    XrResult result = s.next.locateSpace(space, baseSpace, time, location);
    if (XR_FAILED(result) || !location)
        return result;
    std::lock_guard lock(s.mutex);
    RecordAimPose(s, space, baseSpace, time, location->locationFlags, location->pose);
    SpoofHandPose(s, space, baseSpace, location->locationFlags, location->pose);
    return result;
}

XrResult XRAPI_CALL HookLocateSpaces(XrSession session, const XrSpacesLocateInfo* locateInfo, XrSpaceLocations* spaceLocations)
{
    LayerState& s = State();
    XrResult result = s.next.locateSpaces(session, locateInfo, spaceLocations);
    if (XR_FAILED(result) || !locateInfo || !spaceLocations || !spaceLocations->locations)
        return result;
    std::lock_guard lock(s.mutex);
    uint32_t count = std::min(locateInfo->spaceCount, spaceLocations->locationCount);
    for (uint32_t i = 0; i < count; ++i)
    {
        XrSpaceLocationData& location = spaceLocations->locations[i];
        RecordAimPose(s, locateInfo->spaces[i], locateInfo->baseSpace, locateInfo->time, location.locationFlags, location.pose);
        SpoofHandPose(s, locateInfo->spaces[i], locateInfo->baseSpace, location.locationFlags, location.pose);
    }
    return result;
}

XrResult XRAPI_CALL HookLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput,
                                    uint32_t* viewCountOutput, XrView* views)
{
    LayerState& s = State();
    XrResult result = s.next.locateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
    if (XR_FAILED(result) || !viewLocateInfo || !viewState || !viewCountOutput || !views || viewCapacityInput == 0)
        return result;
    if (!(viewState->viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
        return result;
    std::lock_guard lock(s.mutex);
    RecordHead(s, viewLocateInfo->space, viewLocateInfo->displayTime, views, std::min(*viewCountOutput, viewCapacityInput));
    return result;
}

XrResult XRAPI_CALL HookSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo)
{
    LayerState& s = State();
    XrResult result = s.next.syncActions(session, syncInfo);
    if (XR_FAILED(result))
        return result;

    std::vector<std::string> commands;
    {
        std::lock_guard lock(s.mutex);
        if (!s.loggedFirstSync)
        {
            s.loggedFirstSync = true;
            Log("first xrSyncActions (%d)", result);
        }
        ++s.frame;
        UpdateMenuState(s);
        UpdateReloadState(s);
        UpdateGameVars(s);
        UpdateTriggers(s);
        UpdateWeaponButtons(s, session, commands);
        AdvanceWeaponSwitch(s);
        if (s.squeeze != XR_NULL_HANDLE)
        {
            UpdateSqueeze(s, session);
            UpdateGunHand(s);
            UpdateGrenade(s);
        }
        UpdateCrouch(s, session, commands);
        UpdateCommandAction(s, session, s.tactical, s.tacticalButton, "tactical", s.config.tacticalPressCommand, s.config.tacticalReleaseCommand,
                            commands);
        UpdateCommandAction(s, session, s.detonate, s.detonateButton, "detonate", kDetonateCommand, {}, commands);
        UpdateTacticalHand(s);
        UpdateTriggerOverride(s, session);
        UpdateSprint(s, session);
    }

    for (const std::string& command : commands)
    {
        bool sent = EngineExecute(command.c_str());
        Log("command '%s' %s", command.c_str(), sent ? "sent" : "not sent");
    }
    return result;
}

XrResult XRAPI_CALL HookGetActionStateFloat(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateFloat* state)
{
    LayerState& s = State();
    XrResult result = s.next.getActionStateFloat(session, getInfo, state);
    if (XR_FAILED(result) || !getInfo || !state)
        return result;

    std::lock_guard lock(s.mutex);
    if (s.trigger != XR_NULL_HANDLE && getInfo->action == s.trigger)
    {
        const TriggerOverride& o = s.triggerOverride;
        if (!o.applies || getInfo->subactionPath != s.tacticalHand)
            return result;
        state->isActive = XR_TRUE;
        state->currentState = o.output;
        state->changedSinceLastSync = o.changed ? XR_TRUE : XR_FALSE;
        if (o.changed && o.changeTime != 0)
            state->lastChangeTime = o.changeTime;
        return result;
    }

    if (s.squeeze == XR_NULL_HANDLE || getInfo->action != s.squeeze)
        return result;
    if (s.loggedSqueezeQueries < kMaxLoggedQueries)
    {
        ++s.loggedSqueezeQueries;
        Log("app queried squeeze with subaction path %s", PathText(s, getInfo->subactionPath).c_str());
    }

    GripSlot* grip = FindGrip(s, getInfo->subactionPath);
    if (!grip || !grip->sampled)
        return result;
    state->isActive = XR_TRUE;
    state->currentState = grip->output;
    state->changedSinceLastSync = grip->changed ? XR_TRUE : XR_FALSE;
    if (grip->changed && grip->changeTime != 0)
        state->lastChangeTime = grip->changeTime;
    return result;
}

XrResult XRAPI_CALL HookGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateBoolean* state)
{
    LayerState& s = State();
    XrResult result = s.next.getActionStateBoolean(session, getInfo, state);
    if (XR_FAILED(result) || !getInfo || !state)
        return result;

    std::lock_guard lock(s.mutex);
    bool watched = (s.jump != XR_NULL_HANDLE && getInfo->action == s.jump) || (s.stickClick != XR_NULL_HANDLE && getInfo->action == s.stickClick);
    if (watched && s.loggedBooleanQueries < kMaxLoggedQueries * 2)
    {
        ++s.loggedBooleanQueries;
        Log("app queried %s with subaction path %s", getInfo->action == s.jump ? "jump button" : "stick_click", PathText(s, getInfo->subactionPath).c_str());
    }
    if (s.jump != XR_NULL_HANDLE && getInfo->action == s.jump && getInfo->subactionPath == s.jumpHand)
    {
        const CrouchState& c = s.crouchState;
        if (!c.jumpOverride && !c.jumpChanged)
            return result;
        state->isActive = XR_TRUE;
        state->currentState = c.jumpOutput ? XR_TRUE : XR_FALSE;
        state->changedSinceLastSync = c.jumpChanged ? XR_TRUE : XR_FALSE;
        return result;
    }
    if (s.stickClick == XR_NULL_HANDLE || getInfo->action != s.stickClick || getInfo->subactionPath != s.sprintHand)
        return result;
    const SprintState& sprint = s.sprint;
    if (!sprint.pressed && !sprint.changed)
        return result;
    state->isActive = XR_TRUE;
    state->currentState = sprint.output ? XR_TRUE : XR_FALSE;
    state->changedSinceLastSync = sprint.changed ? XR_TRUE : XR_FALSE;
    if (sprint.changed && sprint.changeTime != 0)
        state->lastChangeTime = sprint.changeTime;
    return result;
}

XrResult XRAPI_CALL HookGetActionStateVector2f(XrSession session, const XrActionStateGetInfo* getInfo, XrActionStateVector2f* state)
{
    LayerState& s = State();
    XrResult result = s.next.getActionStateVector2f(session, getInfo, state);
    if (XR_FAILED(result) || !getInfo || !state)
        return result;

    std::lock_guard lock(s.mutex);
    if (s.stick != XR_NULL_HANDLE && getInfo->action == s.stick && s.loggedStickQueries < kMaxLoggedQueries)
    {
        ++s.loggedStickQueries;
        Log("app queried stick with subaction path %s", PathText(s, getInfo->subactionPath).c_str());
    }
    if (s.stick == XR_NULL_HANDLE || getInfo->action != s.stick || getInfo->subactionPath != s.sprintHand)
        return result;
    if (s.sprint.stickZeroed)
    {
        state->currentState = {0.0f, 0.0f};
        state->changedSinceLastSync = s.sprint.stickChanged ? XR_TRUE : state->changedSinceLastSync;
    }
    else if (s.sprint.stickChanged)
        state->changedSinceLastSync = XR_TRUE;
    return result;
}

struct Hook
{
    const char* name;
    PFN_xrVoidFunction function;
};

const Hook kHooks[] = {
    {"xrGetInstanceProcAddr", reinterpret_cast<PFN_xrVoidFunction>(&LayerGetInstanceProcAddr)},
    {"xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction>(&HookDestroyInstance)},
    {"xrCreateActionSet", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateActionSet)},
    {"xrCreateAction", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateAction)},
    {"xrSyncActions", reinterpret_cast<PFN_xrVoidFunction>(&HookSyncActions)},
    {"xrGetActionStateFloat", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateFloat)},
    {"xrGetActionStateBoolean", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateBoolean)},
    {"xrGetActionStateVector2f", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateVector2f)},
    {"xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateReferenceSpace)},
    {"xrCreateActionSpace", reinterpret_cast<PFN_xrVoidFunction>(&HookCreateActionSpace)},
    {"xrLocateSpace", reinterpret_cast<PFN_xrVoidFunction>(&HookLocateSpace)},
    {"xrLocateSpaces", reinterpret_cast<PFN_xrVoidFunction>(&HookLocateSpaces)},
    {"xrLocateViews", reinterpret_cast<PFN_xrVoidFunction>(&HookLocateViews)},
};

XrResult XRAPI_CALL LayerGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function)
{
    if (!name || !function)
        return XR_ERROR_VALIDATION_FAILURE;

    LayerState& s = State();
    if (s.active)
    {
        for (const Hook& hook : kHooks)
        {
            if (std::strcmp(hook.name, name) == 0 && (hook.function != reinterpret_cast<PFN_xrVoidFunction>(&HookLocateSpaces) || s.next.locateSpaces))
            {
                *function = hook.function;
                return XR_SUCCESS;
            }
        }
    }

    if (!s.next.getInstanceProcAddr)
    {
        *function = nullptr;
        return XR_ERROR_HANDLE_INVALID;
    }
    return s.next.getInstanceProcAddr(instance, name, function);
}

XrResult XRAPI_CALL LayerCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo, XrInstance* instance)
{
    if (!layerInfo || !layerInfo->nextInfo || !layerInfo->nextInfo->nextGetInstanceProcAddr || !layerInfo->nextInfo->nextCreateApiLayerInstance)
        return XR_ERROR_INITIALIZATION_FAILED;

    InitializeOnce();
    LayerState& s = State();

    XrApiLayerCreateInfo chained = *layerInfo;
    chained.nextInfo = layerInfo->nextInfo->next;
    XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &chained, instance);
    if (XR_FAILED(result))
    {
        Log("next xrCreateApiLayerInstance failed (%d)", result);
        return result;
    }

    std::lock_guard lock(s.mutex);
    s.instance = *instance;
    s.next = {};
    s.next.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    if (!s.active)
        return result;

    if (info)
        Log("instance created for app '%s' engine '%s'", info->applicationInfo.applicationName, info->applicationInfo.engineName);

    bool resolved = ResolveNext(s, "xrDestroyInstance", s.next.destroyInstance) &&
                    ResolveNext(s, "xrStringToPath", s.next.stringToPath) &&
                    ResolveNext(s, "xrPathToString", s.next.pathToString) &&
                    ResolveNext(s, "xrCreateActionSet", s.next.createActionSet) &&
                    ResolveNext(s, "xrCreateAction", s.next.createAction) &&
                    ResolveNext(s, "xrSyncActions", s.next.syncActions) &&
                    ResolveNext(s, "xrGetActionStateFloat", s.next.getActionStateFloat) &&
                    ResolveNext(s, "xrGetActionStateBoolean", s.next.getActionStateBoolean) &&
                    ResolveNext(s, "xrGetActionStateVector2f", s.next.getActionStateVector2f) &&
                    ResolveNext(s, "xrCreateReferenceSpace", s.next.createReferenceSpace) &&
                    ResolveNext(s, "xrCreateActionSpace", s.next.createActionSpace) &&
                    ResolveNext(s, "xrLocateSpace", s.next.locateSpace) &&
                    ResolveNext(s, "xrLocateViews", s.next.locateViews);
    if (!resolved)
    {
        Log("layer disabled: next functions unavailable");
        s.active = false;
        return result;
    }

    s.leftHand = ToPath(s, "/user/hand/left");
    s.rightHand = ToPath(s, "/user/hand/right");
    s.tacticalHand = s.config.tacticalHand == "left" ? s.leftHand : s.config.tacticalHand == "right" ? s.rightHand : XR_NULL_PATH;
    PFN_xrVoidFunction locateSpaces = nullptr;
    if (XR_SUCCEEDED(s.next.getInstanceProcAddr(s.instance, "xrLocateSpaces", &locateSpaces)))
        s.next.locateSpaces = reinterpret_cast<PFN_xrLocateSpaces>(locateSpaces);
    s.jumpHand = s.rightHand;
    s.sprintHand = s.config.sprintHand == "left" ? s.leftHand : s.config.sprintHand == "right" ? s.rightHand : XR_NULL_PATH;
    s.gunHand = s.rightHand;
    if (s.config.tacticalHand == "auto")
        s.tacticalHand = s.leftHand;
    Log("gun hand right until a gun is drawn or picked up");
    return result;
}
}
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request)
{
    (void)layerName;
    if (!loaderInfo || !request)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo))
        return XR_ERROR_INITIALIZATION_FAILED;
    if (request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST || request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        request->structSize != sizeof(XrNegotiateApiLayerRequest))
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION || loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;

    XrVersion apiVersion = std::min<XrVersion>(XR_CURRENT_API_VERSION, loaderInfo->maxApiVersion);
    if (apiVersion < loaderInfo->minApiVersion)
        return XR_ERROR_INITIALIZATION_FAILED;

    tf2vr::InitializeOnce();
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = apiVersion;
    request->getInstanceProcAddr = tf2vr::LayerGetInstanceProcAddr;
    request->createApiLayerInstance = tf2vr::LayerCreateApiLayerInstance;
    return XR_SUCCESS;
}
