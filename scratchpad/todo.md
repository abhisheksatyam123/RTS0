## Systems

### Android Readiness Assessment
1. **Repository & Build Configurations**: **READY**
   - `export_presets.cfg`: Configured for `Android`, 64-bit architectures (`arm64-v8a`, `x86_64`), Gradle build enabled, launcher icon enabled (`show_as_launcher_app=true`), package name `com.abhisheksatyam123.rts0`.
   - `project.godot`: Mobile rendering override added (`renderer/rendering_method.mobile="mobile"`), ensuring Vulkan Mobile rendering on Android instead of unsupported desktop Forward+.
   - C# GPU compute (`SeparationCompute.cs`): Graceful fallback to CPU if Vulkan compute is unavailable on device.

2. **CI Pipeline (`.github/workflows/ci.yml`)**: **READY**
   - GitHub Actions workflow is fully set up for Android export on `workflow_dispatch` (with `android: true`).
   - Downloads & installs Godot 4.3-stable .NET, export templates (`4.3.stable.mono`), JDK 17, .NET 6.0 SDK, Android SDK platform 34 & build-tools 34.0.0, generates a debug keystore, installs Android build template, and exports `RTS0.apk` downloadable as a build artifact.

3. **Local Workstation Environment**: **NEEDS SETUP IF BUILDING LOCALLY**
   - Godot 4.3 .NET editor binary and .NET 6.0 SDK are not installed on this local workstation PATH.
   - JDK 17, `adb`, and Android SDK tools are present under `/home/abhi/miniconda3/envs/0ad/`.

## Tasks

- [x] Audit repo configs (`project.godot`, `export_presets.cfg`, C# code).
- [x] Fix renderer & launcher configuration for Android.
- [x] Verify CI pipeline readiness for automated APK creation.
- [x] Provide readiness report and instructions for building & testing on Android.

### 2026-04 0 A.D. art bake — complete the game

- [x] PMD binary parser (PSMD `source/graphics/ModelDef.cpp` v1-v5, `CFileUnpacker`) + DDS DXT1/5 decoder (fix G6 channel).
- [x] Actor composer (variant groups, shared `variants/**`, prop attach `root/head/shield/weapon/helmet`, bone-relative `Trs * boneBind`).
- [x] Player-color bake (`shaders/glsl/model_common.fs` alpha-mask `mix(playerColor,white,tex.a)`) + AO multiply + winding CW->CCW + ground-center normalize.
- [x] Blender headless validation: 6 GLBs import cleanly, Cycles renders lit (villager blue 1.7m, swordsman red 1.8m w/ helm/shield/xiphos, town_center 5.2m, house 2.1m, oak 4.5m, gold 1.8m).
- [x] `tools/extract_0ad_assets.py` + `assets/models/manifest.json` (sources, size_meters) + terrain tiles `grass/dirt/rock.png` + `music/{theme,victory,defeat}.ogg`.
- [x] `src/Core/Assets/Assets.cs` + `src/Presentation/WorldView.cs` real-model path (IsRealModel guard, no mat override, Y-offsets), `src/Main.cs` music + picking fix + ghost real mesh.
- [x] Docs: `assets/README.md`, `docs/0AD_ATTRIBUTION.md` (art table), `docs/NEXT_STEPS.md` P1+P3 flipped to ✓, generator cli.
- [ ] Regenerate decision: keep AO bake or lerp 50% (house/civic centre currently dark). Wiring terrain shader to sample `assets/textures/terrain/*.png` (currently stored, not yet sampled) and adding UI/attack sfx from 0ad audio groups are optional polish before showing.
