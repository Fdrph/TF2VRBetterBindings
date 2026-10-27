# TF2VR Better Bindings

Extra SteamVR actions and optional features for [CircuitLord's Titanfall 2 VR mod](https://github.com/CircuitLord/CircuitLordVRModInstaller). **Nothing changes until you bind an action or turn a feature on.** Works left-handed too.

## Actions

Bind them in SteamVR → Settings → Controllers → **Manage Controller Bindings** → Titanfall 2 VR → **Edit**.

| Action | What it does | Bind it to |
|---|---|---|
| **Reload Grab** | Grab magazines and rack slides with the trigger. While you reload, Grip on that hand grabs nothing, so Grip only holds the gun | Each **Trigger**, *Use as Button*, next to the mod's own trigger |
| **Crouch** | Crouch | Any button: *Use as Button* to hold, *Use as Toggle Button* to toggle |
| **Tactical Ability** | Cloak. The trigger of the hand without the gun then stops cloaking | Any button |
| **Next Weapon** | Steps through primary → secondary → arc tool → empty hands, skipping what you don't carry. Change the order with `[weapon_switch] cycle` | Any button |
| **Draw Primary Weapon** / **Draw Secondary Weapon** | Draws that gun | Any button |

Tip: set the mod's **VR Settings → Gun Grip** to **Toggle** to hold your gun with one press of Grip.

### Holster learning and bigger holsters

Every time you draw a gun or take a grenade by hand, the add-on remembers where your hand was, relative to your head, and saves it to `TF2VRBetterBindings.holsters.ini` next to the layer. The weapon buttons use these positions: they briefly move your hand into the holster and squeeze grip for you, so **draw each gun once by hand (with each hand you use) before the buttons work**. Until then the buttons fall back to a console command, which may switch the gun without putting it in your hand.

With `[holsters] assist=1`, a grip press within `assist_radius_cm` (default 15) of a learned holster grabs from that holster, so the holsters act bigger. It never applies to the hand holding a gun, while reloading, or while you point at a pickup.

Keep `TF2VRBetterBindings.holsters.ini` when you update the add-on.

> [!IMPORTANT]
> **Optional features.** Turn them on in `TF2VR\packages\Nighteyes-TF2VRBetterBindings-<version>\plugins\layer\TF2VRBetterBindings.ini`, then restart the game.
>
> | Setting | What it does |
> |---|---|
> | `[sprint] enabled=1` | Sprint by holding your gun low; raise it to aim and the sprint stops |
> | `[grenade] toggle=1` | Grenades toggle like guns: press Grip to take one, press it again to throw. Needs **Gun Grip** on **Toggle** |
> | `[crouch] uncrouch_on_jump=1` | Jumping out of a crouch or slide stands you up and jumps |

<details>
<summary>All other settings</summary>

| Setting | Default | What it does |
|---|---|---|
| `[tactical] block_hand_trigger_in_game` | `1` | `0` = the trigger keeps cloaking after you bind **Tactical Ability** (it still never cloaks during a reload while **Reload Grab** is bound) |
| `[crouch] mode` | `toggle` | Set to `button` if **Crouch** is bound as *Use as Button* and `uncrouch_on_jump` is on |
| `[crouch] jump_delay_ms` | `250` | Raise it if jumping out of a crouch only stands you up |
| `[sprint] start_below_eyes_cm` | `30` | How far below your eyes the gun must be to sprint |
| `[sprint] stop_below_eyes_cm` | `20` | How close to your eyes the gun must come to stop |
| `[reload_grab] enabled` | `1` | `0` = remove the **Reload Grab** action |
| `[weapon_switch] cycle` | `primary,secondary,arc_tool,empty` | What **Next Weapon** steps through, in order. Any of `primary`, `secondary`, `arc_tool`, `grenade`, `empty` (holster the gun) |

</details>

This is an unofficial add-on. It does not modify or include any of CircuitLord's files, and it is not affiliated with CircuitLord, Respawn, EA or Northstar.

## Install

1. Download `Nighteyes-TF2VRBetterBindings-<version>.zip` from the [Releases](../../releases) page. You need Titanfall 2 VR installed with CircuitLord's installer, and SteamVR.
2. Delete any older `Nighteyes-TF2VRBetterBindings-<version>` folder (copy its `TF2VRBetterBindings.ini` first to keep your settings). Extract the zip into `<Titanfall 2 folder>\TF2VR\packages\`, so you get `TF2VR\packages\Nighteyes-TF2VRBetterBindings-<version>\plugins\...`.
3. **START THE GAME, QUIT, AND START IT AGAIN! IT DOES NOT WORK ON THE FIRST START!** This also applies after every update.
4. Bind the actions you want in SteamVR.

## Troubleshooting

- **The actions are missing in SteamVR**: start the game a second time, then reopen the binding editor. `TF2VR\logs\nslog*.txt` has lines tagged `[TF2VRBB]` that say whether the add-on is active.
- **Running the game as administrator** stops the add-on from loading. Start it normally.

## Uninstall

Delete the `Nighteyes-TF2VRBetterBindings-<version>` folder. A leftover registry entry under `HKEY_CURRENT_USER\Software\Khronos\OpenXR\1\ApiLayers\Implicit` is harmless; `unregister-layer.ps1` in the package removes it.

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
