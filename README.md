# Resident Evil 3DD Mod: source of d3d9.dll

This repository holds the source code of `files\d3d9.dll`, the only compiled file in
[Resident Evil 3DD Mod](https://www.nexusmods.com/residentevil3nemesis/mods/136) 1.50 and its Lite
edition (the same file), a fan remaster of the GOG and Steam releases of Resident Evil 3 (1999).

Everything else in the mod archive is data (PNG images, `.r3mesh` / `.r3face` model files,
JSON and text tables, one `.vag` sound, WAV hit sounds) or plain-text scripts. The installer scripts and the
DLL's config file are in `package/`, byte for byte as they are in the archive.

| File in the 1.50 release | Size | SHA-256 |
|---|---|---|
| `files\d3d9.dll` | 2,246,656 bytes | `2cc902216046c9d750d374738ea278f81f61d0a4e7b8194d20181d8afc7624f1` |

## What the DLL does

- The installer copies it next to `ResidentEvil3.exe`. GOG's own DirectDraw wrapper
  (`ddraw.dll`, shipped with the game) renders through Direct3D 9 and loads this `d3d9.dll`
  from the game folder. The DLL loads the real `d3d9.dll` from the Windows system folder and
  passes the Direct3D calls through to it.
- It identifies the game executable (`src/exe_map.h`, GOG 1.0 hotfix4 builds of
  `ResidentEvil3.exe` and `RE3_MERCE.exe`; the English Steam release ships the same builds
  wrapped by Steam's protection, recognised in memory by a hash of the unpacked code) and only
  then hooks the game's renderer in memory (function tables and a few call sites). The hooks
  draw the mod's HD backgrounds and masks,
  HD interface, the 3D models with 60 fps interpolation, and keep the movies in sync. The game
  files on disk are never changed. F8 toggles original/mod, F7 toggles 60/30 fps.
- Since 1.18 the HD models are skinned, lit and projected on the graphics card, in a `vs_3_0`
  vertex shader (`src/model_gpu.h`); the CPU only computes the bone matrices. The shaders are
  compiled at start with `d3dcompiler_47.dll` from the Windows system folder. Without shader
  model 3, or with `gpu_models=0` in `pilot.ini`, the models take the earlier CPU path.
- Since 1.19 the HD backgrounds and mask atlases are read from `.r3tx` files: DXT5 blocks with
  scaled YCoCg colour (`src/r3tx_format.h`, `src/pack_texture.h`), turned back into RGB by a pixel
  shader (`src/state_shader.h`). `render_width` / `render_height` in `pilot.ini` set the size the
  game renders at (2560x1920 by default, 1440x1080 in the Lite edition); the mask atlas scale
  follows the background size.
- Since 1.20 a zombie can be drawn from separate head, upper body and lower body meshes listed in
  `model-pack\kit.txt` (`src/model_integration.h`): the parts are picked per room visit so that
  heads do not repeat within a room, and bullet wounds are placed on the part that was hit
  (`src/model_wounds.h`).
- Since 1.21 the DLL draws HD blood (`src/blood_fx.h`: pools under the dead in place of the
  native shadow stain, hit sprays, exit wounds), plays bullet hit sounds through DirectSound
  (`src/sfx_hits.h`, `model-pack\hit-sounds\hits.txt`), draws the game's own fire, muzzle flash
  and Nemesis gas sprites from sharper redrawn texture pages (`src/fx_fire.h`, `src/fx_muzzle.h`,
  `src/fx_nemesis_cloud.h`), and opened the game's PC settings window with F5
  (`src/options_menu.h`; since 1.50 that window is a row of the mod's menu). Each has its own switch in `pilot.ini` (`blood_fx`, `hit_sounds`,
  `fire_fx`, `muzzle_fx`, `nemesis_cloud_fx`, `options_key`).
- Since 1.50 F5 (or Start on a gamepad) opens the mod's own menu (`src/mod_menu.h`, `src/mod_menu_ui.h`,
  `src/mod_menu_exit.h`): all options of `pilot.ini` with descriptions, rebinding of the game's
  keyboard and gamepad actions (written to `bio3.ini` as the game's own settings window does,
  `src/mod_input.h`) and of the mod's keys, the game's PC settings window, exit to the title
  screen or the desktop. One hook after the game's input poll (`src/mod_hooks.h`) runs the menu,
  a manual reload (`src/reload.h`) and the optional modern, camera-relative controls
  (`src/controls_modern.h`). `src/widescreen.h` crops the picture to 16:9 (centred or following
  the player, with a right-stick look) while keeping text, menus and scenes whole;
  `src/player_movies.h` plays the player's own movie files from a `3DD-Movies` folder instead of
  the game's movies; `src/fx_pages.h` gives water, acid and creature blood sprites sharper texture
  pages; `src/focus_guard.h` keeps the GOG DirectDraw wrapper from rebuilding the display on
  device-change messages (a hang after focus changes). The experimental lighting (dynamic
  shadows of the HD models on the floor and on the models, light of muzzle flashes) lives in
  `src/model_gpu.h`, `src/model_integration.h` and `src/fx_muzzle.h`.
- Also since 1.50: voice packs. WAV files in `3DD-Voice\<pack>\` named like the game's voice clips
  (`src/voice_clips.h`) are served to the game in place of its own clips when the pack is chosen in the
  menu (`src/voice_packs.h`: discovery, `pack.ini`; `src/voice_wav.h`: PCM 16/24/32-bit, float and
  MS-ADPCM converted to the layout the game's WAV parser takes). The lip-sync track of a pack clip is
  computed in the DLL on a worker thread (`src/lipsync_auto.h`, the same analysis the mod's offline tool
  uses for the English tracks).
- Also in 1.50: material files of a model pack give Nemesis' black suit a leather shading
  (`leather.txt`, pixel shader `LEATHER` in `src/model_gpu.h`) and door handles a metal one
  (`metal.txt`); the purple slime at Nemesis' hand is drawn as a translucent volumetric liquid
  (`src/fx_slime_sim.h` turns the game's own slime sprites into round cones, the `SlimeSource`
  pixel shader in `src/model_gpu.h` sphere-traces them, `src/fx_nemesis_cloud.h` drives it); fire
  lights the room and the HD models near it (`src/fx_fire.h`, `src/model_integration.h`); background
  masks in front of a shadow hide it and a floor drawn as a mask keeps it (`src/model_integration.h`).
  Switches in `pilot.ini`: `leather_fx`, `nemesis_slime`, `nemesis_slime_brightness`, `fire_light`,
  `shadow_over_masks`.
- It does not use the network, start processes or touch the registry. It writes only inside the
  game folder: `runtime.log`, `crash-dumps\*.dmp` after a crash, a `captures\` folder that
  stays empty unless a QA capture is switched on in `pilot.ini`, and `pilot.ini` / `bio3.ini`
  when settings are changed in its menu.

## Building

Requirements: Windows 10 or 11 and Visual Studio (Community is enough) or Build Tools for
Visual Studio with the "Desktop development with C++" workload (MSVC x86/x64 tools and a
Windows SDK). The release was built with Visual Studio 2026, MSVC 19.51.36252; other recent
MSVC versions were not tested.

```
build.cmd
```

The script finds Visual Studio with `vswhere`, sets up the x86 environment and runs:

```
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /LD src\proxy.cpp /Fo:"build\proxy.obj" /link /DEF:src\d3d9.def /OUT:"build\d3d9.dll" /IMPLIB:"build\proxy.lib" user32.lib ole32.lib windowscodecs.lib
```

The output is `build\d3d9.dll` (32-bit, static CRT). `src\proxy.cpp` is the single translation
unit; it includes the other headers. `src\d3d9.def` exports `Direct3DCreate9`.

### Comparing with the released file

These sources rebuilt with MSVC 19.51.36252 in the folder the release was built in give a
file that differs from the released `d3d9.dll` only in the link timestamp (in the PE header
and in the debug directory). Built from a different folder path, the size stays 2,246,656
bytes, but the compiler lays the code out in a different order, so the bytes and the hash
differ.

## Layout

```
build.cmd                    build script
src/                         d3d9.dll source
package/Install.cmd          installer entry point (as in the archive)
package/tools/               Install-3DD.ps1, Uninstall-3DD.ps1, Uninstall.cmd
package/files/pilot.ini      the DLL's settings
package/lite/files/pilot.ini the DLL's settings in the Lite edition (render size 1440x1080)
```

## Notes

The DLL was written with the help of an AI coding assistant (Claude Code), as stated on the
mod page.

Resident Evil 3DD Mod is an unofficial, free, non-commercial fan modification, not affiliated
with, endorsed or sponsored by Capcom, GOG or Valve. Resident Evil and the original game content are
trademarks and copyrights of Capcom Co., Ltd. This repository contains no files of the game.

The source is published for review. All rights reserved; the permissions on the Nexus Mods
page apply.
