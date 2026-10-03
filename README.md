# TF2VR Better Bindings

Extra SteamVR actions for [CircuitLord's Titanfall 2 VR mod](https://github.com/CircuitLord/CircuitLordVRModInstaller), so you can bind them however you like in SteamVR:

| SteamVR action | What it does | Suggested binding |
|---|---|---|
| **Toggle Grip** (per hand) | While on, the game sees that hand gripping, so you don't have to hold the grip button to keep holding your gun or its front grip | Grip → *Toggle Button* (Click from Force) |
| **Crouch** | Crouches while pressed | *Button* = hold to crouch, *Toggle Button* = toggle crouch |
| **Tactical Ability (Cloak)** | Uses your tactical ability (cloak) | Any button you like |

Once **Tactical Ability** is bound, the left trigger stops activating cloak during gameplay, so you can use it for other things. It still works normally on the start screen (T-pose calibration) and in menus.

This is an unofficial add-on. It does not modify or include any of CircuitLord's files, and it is not affiliated with CircuitLord, Respawn, EA or Northstar.

## Requirements

- Titanfall 2 VR installed with CircuitLord's installer (it creates the `TF2VR` folder in your Titanfall 2 directory).
- SteamVR. Tested with Valve Index controllers; other controllers work as long as you bind the actions in SteamVR.

## Install

1. Download `Nighteyes-TF2VRBetterBindings-<version>.zip` from the [Releases](../../releases) page.
2. Extract it into `<Titanfall 2 folder>\TF2VR\packages\`. You should end up with:
   ```
   Titanfall2\TF2VR\packages\Nighteyes-TF2VRBetterBindings-1.0.0\
       mods\Nighteyes.TF2VRBetterBindings\
       plugins\TF2VRBetterBindings.dll
       plugins\layer\...
   ```
3. **START THE GAME, QUIT, AND START IT AGAIN! IT DOES NOT WORK ON THE FIRST START!** The first start only registers the add-on for your Windows user. From the second start onwards it works every time.
4. Bind the actions in SteamVR (next section).

## SteamVR bindings

SteamVR → Settings → Controllers → **Manage Controller Bindings** → Titanfall 2 VR → choose your custom binding → **Edit**:

- **Toggle Grip**: on each hand's **Grip**, add *Use as Toggle Button* → Click → `Toggle Grip` (`tf2vr_grip_lock`). Optional: *Generate click from: Force*.
- **Crouch** (`tf2vr_crouch`): any button. *Use as Button* to hold, *Use as Toggle Button* to toggle.
- **Tactical Ability (Cloak)** (`tf2vr_tactical`): any button.
- Leave the mod's own **trigger** action on both triggers, as in the default binding.
- Optional: to grab magazines, bolts, slides and grenades with a trigger while Toggle Grip holds the gun, add a second *Use as Trigger* entry on that trigger and set **Pull → squeeze**.

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
