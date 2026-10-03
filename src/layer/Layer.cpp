#include "Config.h"
#include "Engine.h"
#include "GameVar.h"
#include "Log.h"

#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
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
constexpr const char* kGameplaySetName = "gameplay";
constexpr const char* kSqueezeActionName = "squeeze";
constexpr const char* kTriggerActionName = "trigger";
constexpr const char* kCrouchActionName = "tf2vr_crouch";
constexpr const char* kCrouchLocalizedName = "Crouch";
constexpr const char* kTacticalActionName = "tf2vr_tactical";
constexpr const char* kTacticalLocalizedName = "Tactical Ability (Cloak)";
constexpr const char* kToggleGripActionName = "tf2vr_grip_lock";
constexpr const char* kToggleGripLocalizedName = "Toggle Grip";
constexpr const char* kToggleGripBindingLeft = "/user/hand/left/input/squeeze/value";
constexpr const char* kToggleGripBindingRight = "/user/hand/right/input/squeeze/value";
constexpr const char* kIndexProfile = "/interaction_profiles/valve/index_controller";
constexpr int kMaxLoggedQueries = 8;

struct NextFunctions
{
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
    PFN_xrStringToPath stringToPath = nullptr;
    PFN_xrPathToString pathToString = nullptr;
    PFN_xrCreateActionSet createActionSet = nullptr;
    PFN_xrCreateAction createAction = nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings = nullptr;
    PFN_xrSyncActions syncActions = nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
};

struct GripSlot
{
    XrPath path = XR_NULL_PATH;
    const char* name = "";
    bool enabled = false;
    bool sampled = false;
    bool toggleActive = false;
    bool toggleOn = false;
    float output = 0.0f;
    bool changed = false;
    XrTime changeTime = 0;
};

struct ButtonState
{
    bool active = false;
    bool pressed = false;
    bool changed = false;
    XrTime changeTime = 0;
};

struct TriggerOverride
{
    bool applies = false;
    bool blocked = false;
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
    XrPath indexProfile = XR_NULL_PATH;
    XrPath crouchBinding = XR_NULL_PATH;
    XrPath toggleBindingLeft = XR_NULL_PATH;
    XrPath toggleBindingRight = XR_NULL_PATH;
    XrPath tacticalHand = XR_NULL_PATH;
    XrAction squeeze = XR_NULL_HANDLE;
    XrAction trigger = XR_NULL_HANDLE;
    XrAction toggleGrip = XR_NULL_HANDLE;
    XrAction crouch = XR_NULL_HANDLE;
    XrAction tactical = XR_NULL_HANDLE;
    std::vector<GripSlot> grips;
    ButtonState crouchButton;
    ButtonState tacticalButton;
    TriggerOverride triggerOverride;
    GameVar menuActiveVar{kMenuActiveVar};
    GameVar startupPhaseVar{kStartupPhaseVar};
    int menuState = -2;
    int startupPhase = -2;
    int loggedSqueezeQueries = 0;
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
        Log("config: toggle_left=%d toggle_right=%d crouch=%d binding=%s press_cmd='%s' release_cmd='%s'", s.config.toggleLeft, s.config.toggleRight,
            s.config.crouchEnabled, s.config.crouchBinding.c_str(), s.config.crouchPressCommand.c_str(), s.config.crouchReleaseCommand.c_str());
        Log("config: tactical=%d hand=%s block_in_game=%d press_cmd='%s' release_cmd='%s'", s.config.tacticalEnabled, s.config.tacticalHand.c_str(),
            s.config.tacticalBlockHandTrigger, s.config.tacticalPressCommand.c_str(), s.config.tacticalReleaseCommand.c_str());
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
        grip.enabled = left ? s.config.toggleLeft : s.config.toggleRight;
        s.grips.push_back(grip);
    }
    for (const GripSlot& grip : s.grips)
        Log("squeeze slot %s path=%s toggle=%d", grip.name, PathText(s, grip.path).c_str(), grip.enabled);
}

void UpdateGrips(LayerState& s, XrSession session)
{
    for (GripSlot& grip : s.grips)
    {
        grip.changed = false;
        grip.sampled = false;
        if (!grip.enabled)
            continue;

        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = s.squeeze;
        get.subactionPath = grip.path;
        XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
        bool squeezeActive = XR_SUCCEEDED(s.next.getActionStateFloat(session, &get, &squeeze)) && squeeze.isActive;

        XrActionStateBoolean toggle{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action = s.toggleGrip;
        grip.toggleActive = s.toggleGrip != XR_NULL_HANDLE && XR_SUCCEEDED(s.next.getActionStateBoolean(session, &get, &toggle)) && toggle.isActive;
        bool toggleOn = grip.toggleActive && toggle.currentState == XR_TRUE;
        if (!squeezeActive && !grip.toggleActive)
            continue;

        grip.sampled = true;
        bool toggleChanged = toggleOn != grip.toggleOn;
        if (toggleChanged)
            Log("grip %s %s", grip.name, toggleOn ? "on" : "off");
        grip.toggleOn = toggleOn;

        float output = toggleOn ? 1.0f : squeezeActive ? squeeze.currentState : 0.0f;
        grip.changed = output != grip.output;
        if (grip.changed)
            grip.changeTime = toggleChanged || !squeezeActive ? toggle.lastChangeTime : squeeze.lastChangeTime;
        grip.output = output;
    }
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

void UpdateTriggerOverride(LayerState& s, XrSession session)
{
    TriggerOverride& o = s.triggerOverride;
    o.changed = false;
    if (s.trigger == XR_NULL_HANDLE || s.tacticalHand == XR_NULL_PATH || !s.tacticalButton.active)
    {
        o.applies = false;
        return;
    }

    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = s.trigger;
    get.subactionPath = s.tacticalHand;
    XrActionStateFloat raw{XR_TYPE_ACTION_STATE_FLOAT};
    bool rawActive = XR_SUCCEEDED(s.next.getActionStateFloat(session, &get, &raw)) && raw.isActive;

    bool blocked = s.config.tacticalBlockHandTrigger && s.menuState == 0;
    if (blocked != o.blocked)
        Log("%s trigger %s", s.config.tacticalHand.c_str(), blocked ? "blocked (gameplay, tactical bound)" : "passes through");
    o.blocked = blocked;

    float output = std::max(s.tacticalButton.pressed ? 1.0f : 0.0f, rawActive && !blocked ? raw.currentState : 0.0f);
    o.changed = !o.applies || output != o.output;
    if (o.changed)
        o.changeTime = s.tacticalButton.changed || !rawActive ? s.tacticalButton.changeTime : raw.lastChangeTime;
    if (s.tacticalButton.changed)
        Log("tactical drives %s trigger = %.1f", s.config.tacticalHand.c_str(), output);
    o.output = output;
    o.applies = true;
}

XrResult XRAPI_CALL LayerGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

XrResult XRAPI_CALL HookDestroyInstance(XrInstance instance)
{
    LayerState& s = State();
    XrResult result = s.next.destroyInstance(instance);
    std::lock_guard lock(s.mutex);
    s.squeeze = XR_NULL_HANDLE;
    s.trigger = XR_NULL_HANDLE;
    s.toggleGrip = XR_NULL_HANDLE;
    s.crouch = XR_NULL_HANDLE;
    s.tactical = XR_NULL_HANDLE;
    s.grips.clear();
    s.crouchButton = {};
    s.tacticalButton = {};
    s.triggerOverride = {};
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
    XrAction toggleGrip = CreateLayerAction(s, *actionSet, kToggleGripActionName, kToggleGripLocalizedName, {s.leftHand, s.rightHand});

    std::lock_guard lock(s.mutex);
    s.tactical = tactical;
    s.crouch = crouch;
    s.toggleGrip = toggleGrip;
    s.tacticalButton = {};
    s.crouchButton = {};
    return result;
}

XrResult XRAPI_CALL HookCreateAction(XrActionSet actionSet, const XrActionCreateInfo* createInfo, XrAction* action)
{
    LayerState& s = State();
    XrResult result = s.next.createAction(actionSet, createInfo, action);
    if (XR_FAILED(result) || !createInfo)
        return result;

    Log("action '%s' type %d subaction paths %u", createInfo->actionName, createInfo->actionType, createInfo->countSubactionPaths);
    if (createInfo->actionType != XR_ACTION_TYPE_FLOAT_INPUT)
        return result;

    std::lock_guard lock(s.mutex);
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

XrResult XRAPI_CALL HookSuggestInteractionProfileBindings(XrInstance instance, const XrInteractionProfileSuggestedBinding* suggested)
{
    LayerState& s = State();
    std::vector<XrActionSuggestedBinding> crouchExtras;
    std::vector<XrActionSuggestedBinding> toggleExtras;
    XrPath profile = XR_NULL_PATH;
    {
        std::lock_guard lock(s.mutex);
        profile = s.indexProfile;
        if (s.crouch != XR_NULL_HANDLE && s.crouchBinding != XR_NULL_PATH)
            crouchExtras.push_back({s.crouch, s.crouchBinding});
        if (s.toggleGrip != XR_NULL_HANDLE && s.toggleBindingLeft != XR_NULL_PATH && s.toggleBindingRight != XR_NULL_PATH)
        {
            toggleExtras.push_back({s.toggleGrip, s.toggleBindingLeft});
            toggleExtras.push_back({s.toggleGrip, s.toggleBindingRight});
        }
    }

    if (!suggested || profile == XR_NULL_PATH || suggested->interactionProfile != profile || (crouchExtras.empty() && toggleExtras.empty()))
        return s.next.suggestInteractionProfileBindings(instance, suggested);

    auto attempt = [&](bool withToggle, bool withCrouch, const char* label) {
        std::vector<XrActionSuggestedBinding> bindings(suggested->suggestedBindings, suggested->suggestedBindings + suggested->countSuggestedBindings);
        if (withToggle)
            bindings.insert(bindings.end(), toggleExtras.begin(), toggleExtras.end());
        if (withCrouch)
            bindings.insert(bindings.end(), crouchExtras.begin(), crouchExtras.end());
        XrInteractionProfileSuggestedBinding extended = *suggested;
        extended.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        extended.suggestedBindings = bindings.data();
        XrResult result = s.next.suggestInteractionProfileBindings(instance, &extended);
        Log("index bindings suggested %s (%u total): %d", label, extended.countSuggestedBindings, result);
        return result;
    };

    XrResult result = attempt(!toggleExtras.empty(), !crouchExtras.empty(), "with all layer actions");
    if (XR_SUCCEEDED(result))
        return result;
    if (!toggleExtras.empty() && !crouchExtras.empty())
    {
        Log("runtime rejected crouch binding %s, retrying with toggle grip only", s.config.crouchBinding.c_str());
        result = attempt(true, false, "with toggle grip only");
        if (XR_SUCCEEDED(result))
            return result;
    }
    Log("runtime rejected the layer bindings, suggesting the original bindings only");
    return s.next.suggestInteractionProfileBindings(instance, suggested);
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
        UpdateMenuState(s);
        if (s.squeeze != XR_NULL_HANDLE)
            UpdateGrips(s, session);
        UpdateCommandAction(s, session, s.crouch, s.crouchButton, "crouch", s.config.crouchPressCommand, s.config.crouchReleaseCommand, commands);
        UpdateCommandAction(s, session, s.tactical, s.tacticalButton, "tactical", s.config.tacticalPressCommand, s.config.tacticalReleaseCommand,
                            commands);
        UpdateTriggerOverride(s, session);
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
    if (!grip || !grip->sampled || (!state->isActive && !grip->toggleActive))
        return result;
    state->isActive = XR_TRUE;
    state->currentState = grip->output;
    state->changedSinceLastSync = grip->changed ? XR_TRUE : XR_FALSE;
    if (grip->changed && grip->changeTime != 0)
        state->lastChangeTime = grip->changeTime;
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
    {"xrSuggestInteractionProfileBindings", reinterpret_cast<PFN_xrVoidFunction>(&HookSuggestInteractionProfileBindings)},
    {"xrSyncActions", reinterpret_cast<PFN_xrVoidFunction>(&HookSyncActions)},
    {"xrGetActionStateFloat", reinterpret_cast<PFN_xrVoidFunction>(&HookGetActionStateFloat)},
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
            if (std::strcmp(hook.name, name) == 0)
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
                    ResolveNext(s, "xrSuggestInteractionProfileBindings", s.next.suggestInteractionProfileBindings) &&
                    ResolveNext(s, "xrSyncActions", s.next.syncActions) &&
                    ResolveNext(s, "xrGetActionStateFloat", s.next.getActionStateFloat) &&
                    ResolveNext(s, "xrGetActionStateBoolean", s.next.getActionStateBoolean);
    if (!resolved)
    {
        Log("layer disabled: next functions unavailable");
        s.active = false;
        return result;
    }

    s.leftHand = ToPath(s, "/user/hand/left");
    s.rightHand = ToPath(s, "/user/hand/right");
    s.indexProfile = ToPath(s, kIndexProfile);
    s.crouchBinding = s.config.crouchBinding.empty() ? XR_NULL_PATH : ToPath(s, s.config.crouchBinding.c_str());
    s.toggleBindingLeft = ToPath(s, kToggleGripBindingLeft);
    s.toggleBindingRight = ToPath(s, kToggleGripBindingRight);
    s.tacticalHand = s.config.tacticalHand == "left" ? s.leftHand : s.config.tacticalHand == "right" ? s.rightHand : XR_NULL_PATH;
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
