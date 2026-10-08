# TF2VR Better Bindings

An add-on for [CircuitLord's Titanfall 2 VR mod](https://github.com/CircuitLord/CircuitLordVRModInstaller) that adds:

- **A separate button for reloading.** Grab magazines and rack slides and bolts with your trigger (**Reload Grab**), so your Grip only ever holds the gun.
- **Sprint by lowering your gun.** Hold the gun low against your chest while moving to sprint; bring it up to aim and the sprint stops. No button needed.
- **Tactical ability (cloak) on any button.** Bind it wherever you like in SteamVR.
- **Crouch on any button**, as hold or toggle. Jumping while crouched or sliding stands you up and jumps.
- **Toggle grip for grenades.** With the mod's Gun Grip set to Toggle, grenades toggle like guns: press Grip to take one, press it again during the throw to let it go.

It detects which hand holds your gun, so it also works left-handed.

### Quick setup

1. In game: **VR Settings → Gun Grip → Toggle**.
2. In SteamVR, bind **squeeze** on Grip, **Reload Grab** on both triggers, and **Tactical Ability** and **Crouch** on any buttons you like (see [SteamVR bindings](#steamvr-bindings)).

### Good to know

**Reloading works like this:**

1. Press drop mag. Your support hand lets go of the front grip by itself.
2. Use the **trigger** to grab the new magazine and rack the slide or bolt. Until the reload is done, **Grip** on that hand grabs nothing.
3. Press **Grip** once to take the front grip again.

**Cloak on the trigger:** the trigger of the hand without the gun still cloaks as usual, except during a reload, where it only grabs. If you also bind **Tactical Ability** to a button, that trigger stops cloaking altogether; set `[tactical] block_hand_trigger_in_game=0` to keep both.

> [!IMPORTANT]
> **Settings** are in `TF2VR\packages\Nighteyes-TF2VRBetterBindings-<version>\plugins\layer\TF2VRBetterBindings.ini`. Restart the game after changing them.
>
> | Setting | Default | What it does |
> |---|---|---|
> | `[reload_grab] enabled` | `1` | `0` = turn off Reload Grab; Grip then grabs everything during reloads too |
> | `[grenade] toggle` | `1` | Grenades toggle like guns while **Gun Grip** is set to Toggle. `0` = grenades are always hold-to-grab |
> | `[tactical] block_hand_trigger_in_game` | `1` | With **Tactical Ability** bound, the trigger of the hand without the gun stops cloaking during gameplay. `0` = it keeps cloaking outside reloads |
> | `[crouch] mode` | `toggle` | Set to `button` if you bind **Crouch** as *Use as Button* (hold to crouch) |
> | `[crouch] uncrouch_on_jump` | `1` | `0` = jumping no longer stands you up |
> | `[crouch] jump_delay_ms` | `250` | Wait between standing up and jumping. Raise it if jumping out of a crouch only stands you up |
> | `[sprint] enabled` | `1` | `0` = no sprinting with a lowered gun |
> | `[sprint] start_below_eyes_cm` | `30` | Sprint when the gun is at least this far below your eyes |
> | `[sprint] stop_below_eyes_cm` | `20` | Stop sprinting when the gun comes back within this distance |

This is an unofficial add-on. It does not modify or include any of CircuitLord's files, and it is not affiliated with CircuitLord, Respawn, EA or Northstar.

## Requirements

- Titanfall 2 VR installed with CircuitLord's installer (it creates the `TF2VR` folder in your Titanfall 2 directory).
- SteamVR. Tested with Valve Index controllers; other controllers work as long as you bind the actions in SteamVR.
- For the gun toggle: Titanfall 2 VR v1.0.14 or newer (VR Settings → Gun Grip → Toggle).

## Install

1. Download `Nighteyes-TF2VRBetterBindings-<version>.zip` from the [Releases](../../releases) page.
2. If you have an older version, delete its `Nighteyes-TF2VRBetterBindings-<version>` folder first. Then extract the zip into `<Titanfall 2 folder>\TF2VR\packages\`. You should end up with:
   ```
   Titanfall2\TF2VR\packages\Nighteyes-TF2VRBetterBindings-1.2.0\
       mods\Nighteyes.TF2VRBetterBindings\
       plugins\TF2VRBetterBindings.dll
       plugins\layer\...
   ```
3. **START THE GAME, QUIT, AND START IT AGAIN! IT DOES NOT WORK ON THE FIRST START!** The first start only registers the add-on for your Windows user. From the second start onwards it works every time. This also applies after upgrading to a new version.
4. Bind the actions in SteamVR (next section).

## SteamVR bindings

SteamVR → Settings → Controllers → **Manage Controller Bindings** → Titanfall 2 VR → choose your custom binding → **Edit**:

- **squeeze**: keep it on each hand's **Grip**, as in the default binding, and remove it from the triggers if you added it there.
- **Reload Grab** (`tf2vr_reload_grab`): on each **Trigger**, add *Use as Button* → Click → `Reload Grab`.
- **Crouch** (`tf2vr_crouch`): any button. *Use as Toggle Button* to toggle; if you bind it as *Use as Button* (hold) instead, set `[crouch] mode=button` (see **Settings** above).
- **Tactical Ability (Cloak)** (`tf2vr_tactical`): any button.
- Leave the mod's own **trigger** action on both triggers, as in the default binding.

## Troubleshooting

- **Nothing happens after installing**: start the game a second time. Check `TF2VR\logs\nslog*.txt` for lines tagged `[TF2VRBB]`; they say whether the layer is active or needs a restart.
- **Running the game as administrator** prevents per-user OpenXR layers from loading. Start it normally.
- **The actions are missing in SteamVR**: start the game once, then reopen the binding editor.

## Uninstall

Delete the `Nighteyes-TF2VRBetterBindings-<version>` folder. A leftover registry entry is harmless (the OpenXR loader skips missing files), but you can remove it with `unregister-layer.ps1` from the package before deleting it, or manually under `HKEY_CURRENT_USER\Software\Khronos\OpenXR\1\ApiLayers\Implicit`.

## Building from source

Requirements: Visual Studio 2022 or 2026 with the C++ workload, CMake 3.24+, Git (CMake fetches the Khronos OpenXR headers).

```
cmake -S . -B build -A x64
cmake --build build --config Release
cmake --build build --config Release --target dist_package
```

`dist_package` produces `build\package\Nighteyes-TF2VRBetterBindings-<version>\` and `build\Nighteyes-TF2VRBetterBindings-<version>.zip`. Configure with `-DTF2VR_PACKAGES_DIR="<Titanfall 2>\TF2VR\packages"` to get an `install_tf2vr` target that copies the package there, keeping an existing `.ini`.

## Credits and license

MIT License, see [LICENSE](LICENSE).

- [CircuitLord](https://github.com/CircuitLord) for Titanfall 2 VR. This add-on requires it but does not contain any of it.
- [Northstar](https://github.com/R2Northstar/NorthstarLauncher) (MIT) for the plugin interface and its documentation of engine internals.
- [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) headers (Apache-2.0), fetched at build time.
