# Fitting Room

ESO-style transmog for Skyrim Special Edition and Anniversary Edition. Save
outfits and wear them over your real gear, so the screen shows the look you
picked while the game keeps scoring the armour you actually have on.

https://www.nexusmods.com/skyrimspecialedition/mods/185342

## This fork: Skyrim SE 1.5.97 (RaceMenu SE 0.4.16)

This branch (`se-1.5.97`) is a fork of Fitting Room 1.2.0 that turns the
**Overlays**, **Skin** and **Shape** pages back on for **Skyrim Special Edition
1.5.97**.

On 1.5.97 the newest RaceMenu is **RaceMenu SE 0.4.16** (skee64.dll 4.0.0.0,
October 2020). Its C++ interfaces are older and laid out differently from the
ones Fitting Room is written against (Overlay v1, Override v1, NiTransform v2,
ActorUpdateManager v0), so upstream Fitting Room switches those three pages off
with "the skee64.dll that loaded is older than this page was built for".

What this branch does:

* On exactly that RaceMenu, the three pages call RaceMenu's own **NiOverride
  Papyrus natives** (`AddNodeOverrideString`, `AddOverrideString`,
  `AddNodeTransformScale`, ...) directly from C++ instead of the newer
  interfaces. RaceMenu stores the values itself, so they save and load like
  anything RaceMenu wrote. See [src/NiOverrideLegacy.h](src/NiOverrideLegacy.h).
* Each native is found by name, its parameter count checked, and only called
  if it points into skee64.dll. If anything does not match, the page stays off
  with the original message instead of calling the wrong code.
* Measured against RaceMenu's source at
  [expired6978/SKSE64Plugins 87a5cad](https://github.com/expired6978/SKSE64Plugins/commit/87a5cadd5c282e790ea6e9cf104bb7aa551fc4dc)
  (the commit 0.4.16 was built from) and the shipped skee64.dll.
* AE / newer RaceMenu builds are unchanged and use the original code path.

Known differences on 1.5.97:

* RaceMenu 0.4.16 has no overlay install callback, so the automatic repair of
  body overlays after RaceMenu's post-load pass (OverlayReconcile) does not run.
* When a new piece of armour is put on, the skin changer re-applies all of the
  actor's armour overrides rather than just that piece's (0.4.16 has no
  per-piece native). The result is the same.

Fixes on top of upstream 1.2.0 (all runtimes):

* **1.1:** opening the Bodies page no longer kills the game when a mod ships a
  BodySlide file whose name your Windows code page cannot represent (for
  example the Korean-named presets in *The Modern Scrolls*). The scan used to
  throw on that name in a background thread, which ended the game with no
  crash log (ucrtbase `0xC0000409`); it now reads past it.

Build with `build.bat` (Visual Studio 2022 or newer, vcpkg at `VCPKG_ROOT`).
The DLL and its data land in `build\deploy`; copy that into your Fitting Room
mod folder, or run `build.bat release "<MO2>\mods\Fitting Room"`.

## Requirements

* [CMake](https://cmake.org/)
	* Add this to your `PATH`
* [Vcpkg](https://github.com/microsoft/vcpkg)
	* Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
* [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
	* Desktop development with C++

## User Requirements

* Skyrim SE 1.5.97, or AE 1.6.317 and later. Support for 1.7.104 is in testing. VR is not supported.
* [SKSE64](https://skse.silverlock.org/) for your runtime
* [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
	* The SE or the AE database, whichever matches your game
* [FLICK](https://www.nexusmods.com/skyrimspecialedition/mods/181603)
	* The UI framework the editor draws on. Install FLICK's AE build on AE.
* [OBody NG](https://www.nexusmods.com/skyrimspecialedition/mods/77016) 4.4.0 or newer, optional, for per-outfit body presets

## Building

```
git clone https://github.com/maartenharms/fitting-room.git
cd fitting-room
cmake --preset release
cmake --build build/release
```

## License

[GPL-3.0](LICENSE)
