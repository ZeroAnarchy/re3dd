# Resident Evil 3DD Mod: source of d3d9.dll

This repository holds the source code of `files\d3d9.dll`, the only compiled file in
[Resident Evil 3DD Mod](https://www.nexusmods.com/residentevil3nemesis/mods/136) 1.17, a fan
remaster of the GOG release of Resident Evil 3 (1999).

Everything else in the mod archive is data (PNG images, `.r3mesh` / `.r3face` model files,
JSON and text tables, one `.vag` sound) or plain-text scripts. The installer scripts and the
DLL's config file are in `package/`, byte for byte as they are in the archive.

| File in the 1.17 archive | Size | SHA-256 |
|---|---|---|
| `files\d3d9.dll` | 1,105,920 bytes | `0f42c4aa4d57c1d6b4b8cb31a39ca909b7b3078c4aed5e615d4003557f13c062` |

## What the DLL does

- The installer copies it next to `ResidentEvil3.exe`. GOG's own DirectDraw wrapper
  (`ddraw.dll`, shipped with the game) renders through Direct3D 9 and loads this `d3d9.dll`
  from the game folder. The DLL loads the real `d3d9.dll` from the Windows system folder and
  passes the Direct3D calls through to it.
- It identifies the game executable (`src/exe_map.h`, GOG 1.0 hotfix4 builds of
  `ResidentEvil3.exe` and `RE3_MERCE.exe`) and only then hooks the game's renderer in memory
  (function tables and a few call sites). The hooks draw the mod's HD backgrounds and masks,
  HD interface, the 3D models with 60 fps interpolation, and keep the movies in sync. The game
  files on disk are never changed. F8 toggles original/mod, F7 toggles 60/30 fps.
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
Built from a different folder path, the size stays 1,105,920 bytes, but the compiler lays the
code out in a different order, so the bytes and the hash differ.

## Layout

```
build.cmd                    build script
src/                         d3d9.dll source
package/Install.cmd          installer entry point (as in the archive)
package/tools/               Install-3DD.ps1, Uninstall-3DD.ps1, Uninstall.cmd
package/files/pilot.ini      the DLL's settings
```

## Notes

The DLL was written with the help of an AI coding assistant (Claude Code), as stated on the
mod page.

Resident Evil 3DD Mod is an unofficial, free, non-commercial fan modification, not affiliated
with, endorsed or sponsored by Capcom or GOG. Resident Evil and the original game content are
trademarks and copyrights of Capcom Co., Ltd. This repository contains no files of the game.

The source is published for review. All rights reserved; the permissions on the Nexus Mods
page apply.
