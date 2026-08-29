# Build & Release Guide

Targets: **Windows, Linux, Android** (automated), **iOS** (macOS-only).

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

## Local builds

```bash
# Windows (PowerShell)
powershell -File tools\build.ps1 windows      # -> build/windows/RTS0.exe

# Linux / macOS (bash); 'all' = windows + linux
GODOT=/path/to/Godot ./tools/build.sh linux   # -> build/linux/RTS0.x86_64
GODOT=/path/to/Godot ./tools/build.sh android # -> build/android/RTS0.apk
```

## CI (GitHub Actions)
`.github/workflows/ci.yml`:
- Every push/PR: builds the C# solution, then exports **Windows + Linux**, uploads artifacts to the run page.
- Manual dispatch ("Run workflow" button): optional checkbox builds the **Android APK** (debug-signed).
- Tag pushes (`v*`) also trigger; hook a release-upload step here when ready.
- iOS intentionally not automated until signing assets exist and the .NET-on-iOS gate passes.

## First-run checklist
1. Open the project in the editor once and visit Project → Export: the editor normalizes any preset fields that drifted from your exact Godot patch version (harmless, expected).
2. Run one desktop export end-to-end locally before trusting CI.
3. Test an Android build on a real device early — emulator GPU behavior differs wildly.

## Known pitfalls
- `dotnet build` succeeds but export fails → almost always template/editor version mismatch.
- Android export hangs in CI → Gradle downloading dependencies; first run takes several minutes.
- Missing console window on Windows debug builds → `debug/export_console_wrapper` writes a second `_console.exe`; ship the main exe only.
