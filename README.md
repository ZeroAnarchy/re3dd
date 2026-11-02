# Resident Evil 3DD Mod: source of d3d9.dll

This repository holds the source code of `files\d3d9.dll`, the only compiled file in
[Resident Evil 3DD Mod](https://www.nexusmods.com/residentevil3nemesis/mods/136) 1.19 and its Lite
edition (the same file), a fan remaster of the GOG and Steam releases of Resident Evil 3 (1999).

Everything else in the mod archive is data (PNG images, `.r3mesh` / `.r3face` model files,
JSON and text tables, one `.vag` sound) or plain-text scripts. The installer scripts and the
DLL's config file are in `package/`, byte for byte as they are in the archive.

| File in the 1.19 archives | Size | SHA-256 |
|---|---|---|
| `files\d3d9.dll` | 1,213,440 bytes | `27ba4ea155f0631e55003479f5cd521ad61a44eec2b9cffcc7be0cfc7c9e6fc8` |

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
- It does not use the network, start processes or touch the registry. It writes only inside the
  game folder: `runtime.log`, `crash-dumps\*.dmp` after a crash, and a `captures\` folder that
  stays empty unless a QA capture is switched on in `pilot.ini`.

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
file that differs from the released `d3d9.dll` only in the 4 bytes of the link timestamp.
Built from a different folder path, the size stays 1,213,440 bytes, but the compiler lays the
code out in a different order, so the bytes and the hash differ.

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
