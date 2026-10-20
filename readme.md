# Windowed Mode plugin (for GTA3, VC, SA)
[DOWNLOAD](https://github.com/ThirteenAG/III.VC.SA.WindowedMode/releases/latest)
----
This plugin allows to run game in window as well in windowed full screen mode.
Originally created by **maxorator** as part of **Maxo's Vehicle Loader** for Vice City.

----
## Installation
1. [ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) is required. If you don't have it, just install [Widescreen Fix](https://thirteenag.github.io/wfp#gta), ASI Loader comes with it.
2. Unpack **III.VC.SA.WindowedMode.asi** and **III.VC.SA.WindowedMode.ini** into **scripts** folder, or [modloader](https://github.com/thelink2012/modloader/releases) folder.
3. Start the game.

----
## Supported games
* Classic GTA III v1.0, v1.1 and Steam executables
* Classic GTA Vice City v1.0, v1.1 and Steam executables
* Classic GTA San Andreas v1.0 US

----
## Hotkeys
* **Alt+Enter**: Toggle between borderless-fullscreen and windowed modes
* **Ctrl+Enter**: Toggle between bordered and borderless window styles

----
## Configuration
Settings are stored beside the plugin in **III.VC.SA.WindowedMode.ini**. Window size, position and style are saved automatically. Existing v2 configuration keys remain supported.

## Native resolution / 4K troubleshooting

**Important difference:** Exclusive fullscreen may render at true 4K without
the mod while a windowed plugin renders at a quarter of the pixel count.
Windows display scaling at 200% is a possible cause. This fork now requests
**per-monitor DPI awareness before the GTA window is created** (INI
`HighDPIAware = 1`) when the process has not already established a DPI mode.
If the game/another mod/manifest has already set DPI awareness, Windows will
reject changing it; that existing setting is respected. To disable the attempt,
set `HighDPIAware = 0` and restart.

For a quick no-code test: GTA executable > Properties > Compatibility >
Change high DPI settings > Override high DPI scaling behavior >
**Application**. Compare image sharpness with the plugin on/off.

**Optional resolution diagnostics:** Disabled by default. Set
`ResolutionDiagnostics = 1` in `[MAIN]` to write
`III.VC.SA.WindowedMode.ini.resolution.log` beside the installed INI,
after the first presented frame and each successful device reset.
The log includes the **physical monitor mode, window client size, Direct3D
backbuffer, RenderWare mode, and camera raster**.
If the display is `3840x2160` but the client/backbuffer reads `1920x1080`,
DPI virtualization or window geometry may be responsible.
No per-frame log writes are performed. Restore `0` to turn logging off.

The distributed INI now defaults to desktop-sized **borderless fullscreen**
(`WindowMode = 3`) with automatic monitor resolution (`ResolutionX = 0`,
`ResolutionY = 0`). For a 3840x2160 desktop this gives a 3840x2160 client area
and matching Direct3D backbuffer; it does **not** force 4K on lower-resolution
monitors. Both dimensions must be zero to enable automatic resolution.

```ini
[MAIN]
WindowMode = 3
WindowedStyle = 1
HighDPIAware = 1
ResolutionDiagnostics = 0

[WINDOW]
ResolutionX = 0
ResolutionY = 0
PositionX = -1
PositionY = -1
```

For a *fixed* 4K request, use `ResolutionX = 3840` and `ResolutionY = 2160`.
`WindowMode = 2` provides a borderless, desktop-sized window when the dimensions
match the display; `WindowMode = 3` always fills the selected monitor.
**WindowMode = 1** is a *bordered* window; on a 3840x2160 monitor it must use
a slightly smaller client area to leave room for its title bar and borders.
The plugin keeps the requested INI size when this happens rather than
silently rewriting it.

If you still see a small or blurry image, check the FPS/window caption for the
actual client size, ensure you edited the INI beside the *loaded* ASI (not a
duplicate in another scripts/modloader folder), and verify the game's own
graphics options / other rendering ASIs. This source cannot guarantee native
4K with incompatible executable versions, render wrappers or DPI overrides.

**Existing INIs are not overwritten automatically.** Update your existing
INI manually to use the new `0/0` desktop mode. The included INI is a template;
back up your customized copy before copying files into the game directory.
Alt+Enter preserves the requested resolution instead of switching it to 640x480.

## Performance and validation
This is a window-management plugin, not an FPS unlocker. Its optimized mouse
handling, cursor synchronization, INI persistence, and caption updates reduce
plugin overhead without changing GTA's game-speed limiter, presentation interval,
or renderer. Results depend on hardware, game version, and other installed mods.

See [OPTIMIZATION_REPORT.md](OPTIMIZATION_REPORT.md) for code changes, known
limitations, and the Windows game-testing checklist. The portable FPS counter
regression test can be built with:

```sh
g++ -std=c++20 -Wall -Wextra tests/test_fps_counter.cpp -o test_fps_counter
./test_fps_counter
g++ -std=c++20 -Wall -Wextra tests/test_resolution_policy.cpp -o test_resolution_policy
./test_resolution_policy
```

----
## Stability update (2026-10-20)

This source now initializes exclusively through the exported `InitializeASI`
function, outside `DllMain`. **Ultimate ASI Loader is required**, configured
with its default `DontLoadFromDllMain=1`. Older ASI loaders that only call
`LoadLibrary` will not initialize this plugin. The static CRT now retains
its required thread notifications.

Explicit `WM_DPICHANGED` and `WM_DISPLAYCHANGE` handling preserves the
already-working native-resolution buffer and INI-requested dimensions when
Windows display geometry changes. Device resets prefer the valid physical
client area and retain the last good size while minimized. Configuration
writes use a temporary sibling file and atomic replacement.

The default INI is unchanged (`ResolutionDiagnostics=0`). For offline
frame-pacing comparisons, export a frame-time CSV using a tool such as
PresentMon, then run:

```sh
python tools/frame_time_report.py capture.csv --target-fps 60
python -m unittest discover -s tests -p test_frame_time_report.py -v
```

The analyzer reads `msBetweenPresents` by default; use `--column` to select
another millisecond-valued column. It does **not** add game-frame work or
modify the game's limiter/VSync. Review [STABILITY_UPDATE.md](STABILITY_UPDATE.md)
for changes, risks, and the Windows test checklist. These source changes have
not yet been compiled/tested on Windows or benchmarked in GTA.

----
## One-command Windows build

On Windows, extract the source ZIP, then double-click **`build.bat`** (or run it from a Command Prompt). The script creates a **Release / Win32** plugin at `data\III.VC.SA.WindowedMode.asi`.

The launcher automatically:

1. Restores missing `IniReader`, `Hooking.Patterns`, and `injector` sources, including the **pinned SafetyHook revision** and bundled Zydis source. It uses Git submodule revisions for a Git checkout; from an extracted source ZIP it clones the upstream repositories with Git, or downloads GitHub ZIP snapshots if Git is not installed.
2. Locates Visual Studio **2026** MSBuild with the **x86 C++** toolchain. If absent, downloads the official Microsoft-signed Build Tools bootstrapper and starts installation of the C++ workload. This may require administrator approval, a large download and/or a Windows restart. No installation is attempted when the needed tools are already available.
3. Runs the included `premake5.exe vs2026`, builds the selected configuration, and verifies that the `.asi` was produced.

Commands:

```bat
build.bat
build.bat Release -Package
build.bat GtaSA
build.bat -NoToolInstall
```

`-Package` on a Release build also invokes `data\release.bat` to create `data\III.VC.SA.WindowedMode.zip`. `-NoToolInstall` fails with an actionable error if the required Microsoft C++ toolchain is missing, rather than installing software. Git is optional for extracted ZIP builds, but recommended for reproducible submodule revisions.

You need Windows 10/11, internet access for initially missing components, and PowerShell 5.1 or later. The build does **not** download the GTA games. If the compiler installer requests a restart, restart Windows and run `build.bat` again.

### Manual build

Alternatively, initialize the Git submodules yourself, run `premake5.bat`, then build the generated solution in `build` with **Visual Studio 2026**, using **Release / Win32**. The project uses C++ modules; game-specific patterns are in `GTA3.ixx`, `GTAVC.ixx` and `GTASA.ixx`.

For local deployment and debugging, create `.env` beside `premake5.lua`, then rerun `premake5.bat`:

```ini
GTAIII_DIR=Z:\WFP\Games\Grand Theft Auto\GTAIII
GRAND_THEFT_AUTO_VICE_CITY_DIR=Z:\WFP\Games\Grand Theft Auto\Grand Theft Auto Vice City
GTA_SAN_ANDREAS_DIR=Z:\WFP\Games\Grand Theft Auto\GTA San Andreas
```

The `Gta3`, `GtaVC` and `GtaSA` configurations use the corresponding path for debugging and copy the built ASI into that game's `scripts` folder only if the plugin is already installed. Quotes and trailing slashes are supported. Missing keys disable deployment and debugger paths for that game. `Release` remains independent of local game paths. The `.env` file is ignored by Git.

For a versioned build, run `premake5.exe vs2026 --with-version=2.2.0` before building. Without an explicit version, the build uses the current date and time. The product version also includes the Git commit hash.

Run `data/release.bat` after a Release build to embed the PDB, optionally sign the ASI, and package it with the INI and documentation. Signing uses a base64-encoded PFX in `CODE_SIGNING_PFX` and its password in `CODE_SIGNING_PASSWORD`; when neither credential is configured, ordinary builds remain unsigned. Embedding runs before signing.

GitHub Actions builds pushes and pull requests and uploads the ZIP and ASI. Configure the two signing secrets for signed builds on `master`. To publish a release, run the workflow manually on `master`, select `release: true`, and choose the major/minor/patch increment. Releases use the next semantic version, matching the binary's version resource.

----
## Credits
* **maxorator** - created original [Vehicle Loader for Vice City](http://gtaforums.com/topic/477801-maxos-vehicle-loader/)
* **ThirteenAG** - ported into stand-alone ASI plugin
* **not6** - fixed Alt key problems
* **Miran** - rewritten into version 2.0

----
## Screenshots
![ScreenShot](http://i.imgur.com/ApT8VH4m.jpg) ![ScreenShot](http://i.imgur.com/R3k5fPGm.jpg)

![ScreenShot](http://i.imgur.com/Q3GkXikm.jpg) ![ScreenShot](http://i.imgur.com/wHbbtM0m.jpg)
