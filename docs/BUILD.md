# Build & Release Guide

Targets: **Linux, Windows, Android, macOS** (all buildable from one Linux host), **iOS** (macOS host with Xcode).

## Version pinning (critical)
Everything must match:
| Component | Value |
|---|---|
| Godot editor | **4.3-stable (Standard edition)** |
| Export templates | `Godot_v4.3-stable_export_templates.tpz` from the official release page |
| C++ Standard | C++20 (`gcc` 11+, `clang` 14+, or MSVC 2022) |
| SCons | 4.x (Python build tool) |

> **Note:** RTS0 is built as a pure C++ GDExtension (`src/`). **.NET / C# / Mono SDK is NOT required.**

## One-time setup per machine

### All platforms
1. Download Godot 4.3-stable **Standard** for your OS: https://github.com/godotengine/godot/releases/tag/4.3-stable
2. Download `Godot_v4.3-stable_export_templates.tpz` from the same page.
3. Install templates:
   - Linux/macOS: extract the `.tpz` (it's a zip) and move the inner `templates/` folder to
     `~/.local/share/godot/export_templates/4.3.stable/`
   - Windows: `%APPDATA%\Godot\export_templates\4.3.stable\`
4. Compile the C++ GDExtension library with SCons: `scons platform=linux target=template_release` (or `template_debug`)

### Android extras
- Android Studio or command-line SDK with: platform `android-34`, build-tools `34.0.0`
- JDK **17**
- In Godot: Editor Settings → Export → Android → set SDK path + (auto-generated) debug keystore.
- Store releases later need your own release keystore + Play Console setup. Never commit keystores.

### iOS extras (macOS only)
- Xcode 15+, Apple Developer account ($99/yr) for device/TestFlight.
- Set team ID + bundle identifier in Project → Export → iOS before archiving.
- The export produces an **Xcode project** you sign/archive in Xcode (or fastlane).

### Cloud containers (headless checks)
A fresh Linux container has no Godot, so the GDScript checks (`tools/e2e_*.gd`) can't run there. After building the GDExtension, `tools/setup_godot.sh --import` downloads the Godot build `ci.yml` uses (4.3-stable from github.com/godotengine/godot releases, checked against its SHA512-SUMS.txt) to `/usr/local/bin/godot`, or to `GODOT_DEST`. It then imports the assets in batches with `tools/godot_import.py`. A fresh checkout took 51 minutes on 4 cores, with Godot peaking at 2.8 GB. Leave out `--import` for checks that load no art. Then run a check with `godot --headless --path . --fixed-fps 60 -s tools/e2e_options.gd`, which exits 0 on a pass. With `godot` on PATH, `scons` re-exports `RTS0.x86_64` after every build unless it is given `skip_export=1`.

## Local builds

`tools/build.py` builds the GDExtension for each target, imports the assets once, and exports
with the matching preset in `export_presets.cfg`. `--package` also writes one file per platform
to `build/dist/`.

```bash
# Windows (PowerShell)
powershell -File tools\build.ps1 windows      # -> build/windows/RTS0.exe

# Linux / macOS (bash)
GODOT=/path/to/Godot ./tools/build.sh linux   # -> build/linux/RTS0.x86_64
./tools/build.sh linux windows android macos --package   # every device build from one Linux host
./tools/build.sh ios                          # macOS + Xcode only -> build/ios/RTS0.xcodeproj
```

What each target needs on the host, beyond Godot 4.3-stable, its export templates, scons and `godot-cpp/`:

| Target | Host tools | Output |
|---|---|---|
| linux | gcc or clang | `RTS0.x86_64` + `librtso.linux…so` |
| windows | MSVC on Windows; `mingw-w64` on Linux/macOS | `RTS0.exe` (pck embedded) + `librtso.windows…dll` |
| android | `ANDROID_HOME` with `ndk/<version>` (godot-cpp defaults to 23.2.8568313; set `ANDROID_NDK_VERSION` for another) plus `build-tools` and `platform-tools`, and a JDK | `RTS0.apk`, arm64, no Gradle |
| macos | Xcode tools on macOS; on Linux `zig` (`pip install ziglang`) and `llvm-lipo` | `RTS0.zip` with a universal, ad-hoc signed `RTS0.app` |
| ios | macOS with Xcode | Xcode project to sign and run from Xcode |

- **Android signing.** The APK is signed with `build/android-keystore/rts0.keystore`, made on the first
  run and never committed, so it sideloads. Updating an installed APK needs the same keystore; set
  `GODOT_ANDROID_KEYSTORE_RELEASE_PATH`, `_USER` and `_PASSWORD` to use your own.
- **macOS on Linux.** There is no macOS SDK on Linux, so `tools/zig-osxcross/` stands in for osxcross:
  zig ships the libSystem headers and stubs (so macOS 11+), the extension is built once per arch and joined with
  `llvm-lipo`. Godot's built-in signer ad-hoc signs the app. It is not notarized, so the first launch
  needs right-click → Open (or `xattr -dr com.apple.quarantine RTS0.app`).
- **iOS.** On a Mac, `python3 tools/build.py ios --ios-team-id <TEAM>` (or `RTS0_IOS_TEAM_ID`) builds
  the extension as `bin/librtso.ios.template_release.xcframework` (extension + godot-cpp in one static
  library) and exports `build/ios/RTS0.xcodeproj`. The team id is in Xcode > Settings > Accounts; it is
  written into the preset for that export only. The preset targets iPhone and iPad (iOS 12+), signs
  for Development, and uses the project icon. Open the project in Xcode, check the team under Signing &
  Capabilities, and run on a connected device. A free Apple ID works for 7-day device installs. The
  Xcode-project export itself also runs on Linux; only the extension needs the Mac.
- **Touch.** On Android and iOS, `scenes/common/touch_input.gd` turns touches into mouse input:
  tap = left click (double tap = double click), drag = box select, hold still or a two-finger tap =
  right click (orders), two fingers = pan, pinch = zoom. Taps and holds work on the minimap and the
  command panel, so buildings are placed by tapping the button, then the ground. Distances are in UI
  pixels, so the same finger travel counts on any screen, and the pointer is parked off the screen
  between touches so the camera never edge-scrolls. The session's touch bar
  (`scenes/session/touch_controls.tscn`) stands in for hotkeys, and its Shift, Ctrl and Alt buttons latch
  those modifiers until tapped again (Shift queues orders and adds to the selection).
  `tools/e2e_touch_input.gd` checks the gestures and `tools/e2e_touch_match.gd` plays a match by touch
  (select, orders, camera, minimap, placing a House), both headless; pass `-- --touch-input` to try it on
  a desktop touch screen.
- **Phone screens.** On a phone (a mobile screen under 7.5 inches across, or `-- --phone-ui` on a
  desktop) `scenes/menu/user_settings.gd` enlarges the whole UI so the layout is at least 1200x540 UI
  pixels: 1.33x on 2400x1080 and 1600x720 phones, on top of the GUI scale option. Tablets and desktops
  keep 1.0, so 1080p is unchanged. On short screens the main menu keeps only the community links that
  fit below its buttons, and the big fixed-size dialogs (encyclopedia, tech tree, save/load) shrink to
  the screen (`scenes/common/fit_screen.gd`). `tools/shot_layouts.gd` screenshots every menu and session
  dialog at a given window size, for checking layouts: `xvfb-run -a -s "-screen 0 2560x1600x24" godot
  --rendering-driver opengl3 --path . -s tools/shot_layouts.gd -- --size 2400x1080 --out DIR --phone-ui`.
- **Duplicate textures.** Godot unpacks each model's embedded images next to it, so about 6,000 images
  are byte-identical copies. The `addons/rts0_export` editor plugin packs each one once and exports the
  copies as `.import` remaps to it, which cut the Linux build from 3.2 GB to 2.1 GB. Byte-identical
  sounds (voices kept under three folders) are packed once the same way. The editor and in-project runs
  are unchanged.
- **Model textures.** `tools/godot_import.py` switches the textures unpacked from models to VRAM
  compression (S3TC on desktop, ETC2 on Android and iOS), as the editor's "Detect 3D" does the first
  time it draws them. A headless import never draws, so without this they stay lossless. Compressed,
  they are about 40% smaller in the pack and a quarter of the GPU memory. Terrain, UI and unit-variant
  textures stay lossless.
- **Mobile renderer and defaults.** Android and iOS run GL Compatibility
  (`rendering/renderer/rendering_method.mobile`), which draws player colours, garrison flags, silhouettes,
  rings and decals (#1260, #1306; `tools/check_gl_shaders.py` checks it). Phones also start with cheaper
  defaults (`kMobileDefaults` in `src/presentation/user_config.cpp`): no shadows, post-processing or
  particles, no water reflection or refraction, 3D at 0.75 resolution (bilinear; FSR is Forward+ only),
  1x anisotropy, and 30 fps in menus and matches. In a match under GL (llvmpipe, 1280x720) the 0.75 scale
  saves about 20% of the frame, shadows about 10% and a third of the draw calls, post-processing about
  6%; water and particles cost little. The options page changes any of them, and Reset returns to these.
  `-- -mobiledefaults` uses them on a desktop. Desktop defaults are 0 A.D.'s.

## CI (GitHub Actions)
`.github/workflows/ci.yml`:
- Every PR and every push to `main`: builds the C++ GDExtension (`template_release`) and runs the headless C++ tests (pathfinder, fixed-point, geometry), the nav parity probe and the lockstep playtest.
- Pushes to `main` and `v*` tags (not PRs): also export **Windows + Linux** and upload the builds and the GDExtension library as artifacts.
  - Assets are imported with `tools/godot_import.py` (see Known pitfalls). The import is cached by a hash of `assets/`, `audio/` and `project.godot`, and each tracked file's modified time is pinned to its git blob first, so a run only imports the assets that changed since the cached one.
  - Each platform runs one export per branch at a time. A newer push waits for it, and a waiting export is dropped when an even newer push arrives.
- A new push to a PR cancels that PR's older run.
- Manual dispatch ("Run workflow" button): the `android` checkbox also builds the **Android APK** (debug-signed).
- iOS is not automated until signing assets exist.

## First-run checklist
1. Open the project in the editor once and visit Project → Export: the editor normalizes any preset fields that drifted from your exact Godot patch version (harmless, expected).
2. Run one desktop export end-to-end locally before trusting CI.
3. Test an Android build on a real device early — emulator GPU behavior differs wildly.

## Known pitfalls
- `scons` succeeds but export fails → almost always a template/editor version mismatch.
- The CI runner dies with "received a shutdown signal" during the import, or a local import gets killed → Godot 4.3 keeps memory from every file it imports until it exits, and a full import of RTS0's assets grows past 10 GB. Import with `python3 tools/godot_import.py` instead of `godot --import` (`tools/build.py` already does). It moves the files Godot would import out of the project and brings them back in batches, one Godot run each; a full import peaks around 2.3 GB.
- Godot reimports everything after a fresh clone or a checkout that touches many files → Godot 4.3 reimports any file whose modified time differs from its file cache, even when the content is the same. `tools/godot_import.py` batches those too.
- Android export hangs in CI → Gradle downloading dependencies; first run takes several minutes.
- Missing console window on Windows debug builds → `debug/export_console_wrapper` writes a second `_console.exe`; ship the main exe only.
