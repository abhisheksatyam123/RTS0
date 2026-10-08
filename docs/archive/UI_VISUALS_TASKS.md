> **Archived 2026-10-08.** Historical record only; do not plan work from it. See [docs/README.md](../README.md) for the live docs.

## Systems

**Project:** RTS0 — High-fidelity Godot 4.3 C++ GDExtension port of 0 A.D.

**Visuals & UI Identity:**
- Typography: Linux Biolinum (`LinBiolinum_Rah.ttf`, `LinBiolinum_RBah.ttf`), 0 A.D.'s authentic typeface.
- Global Theme (`assets/themes/zeroad_theme.tres`):
  - Panels: Stone textured (`stone_background.png`) with 2px gold border (`#e6be50` / `Color(0.9, 0.75, 0.31)`).
  - Buttons: Carved stone button textures (`button_stone_unselected.png` normal, `button_stone_selected.png` hover/pressed).
  - Audio: Official 0 A.D. UI sound effects (`ui_button_click.ogg`, `ui_button_longclick.ogg`).
  - Music: Official 0 A.D. orchestral theme (`honor_bound.ogg`).
- 0 A.D. Parallax Main Menu (`scenes/menu/main_menu.tscn`, `main_menu.gd`):
  - Multi-layer drifting parallax backgrounds: Hellenes (Athens/Acropolis), Carthage (Citadel/Harbor), Kush (Pyramids/Nile), Seleucid (Monumental city).
  - Left navigation stone slab panel with official 0 A.D. logo, Wildfire Games crest, and version info.
  - Submenu popout panels for Learn to Play, Single-player, Multiplayer, Settings.
  - Full functional navigation to Custom Match Setup, Quick Skirmish, Replays, Lobby, Options, Encyclopedia, Asset Viewer, and Credits modal.
- In-Game Visual & Environmental Upgrades:
  - World Environment (`scenes/env.tres`): ACES tonemapping, rich atmospheric depth fog, SSAO for unit/building contact shadows, tuned sky and ground bounce.
  - Sun & Lighting (`scenes/main.tscn`): Warm Mediterranean directional sunlight with soft PCF shadow filters and balanced directional bias.
  - Terrain Shader (`shaders/terrain.gdshader`): Removed artificial grid line overlay; smooth multi-texture blending between grass, dirt, and cliff rocks based on slope and height.
  - Water Rendering (`shaders/water.gdshader`, `world_view_node.cpp`): Dynamic animated waves with dual-frequency wave displacement, turquoise-to-deep-navy Mediterranean gradient, and fresnel specular glistening.
- 0 A.D. Maps Ported (`data/maps/`):
  - Added 15 new map files from 0 A.D.: alpine_valleys, arcadia, belgian_bog, cycladic_archipelago, death_canyon, deccan_plateau, gallic_fields, golden_island, mediterranean_cove, nile_river, persian_highlands, saharan_oases, sicilia, tuscan_acropolis, vesuvius.
  - Updated existing maps: acropolis_bay, atlas_valleys, bactria, battle_for_the_tiber, corinthian_isthmus.
- Presentation & UI C++ Modules (`src/presentation/`):
  - Extracted modular headers: `production_queue.h`, `top_panel.h`.
  - Updated minimap view (`minimap_view.h`, `minimap_view.cpp`) with improved rendering and bounds.
  - Updated camera rig (`camera_rig.h`, `camera_rig.cpp`) with smooth pan/zoom/orbit controls.
  - Integrated full session dialog styling and parity checks.

## Tasks

- [x] Create 0 A.D. Theme (`assets/themes/zeroad_theme.tres`) with Linux Biolinum fonts, stone button styles, and gold accents
- [x] Build 1-to-1 0 A.D. Main Menu (`scenes/menu/main_menu.tscn`, `main_menu.gd`) with multi-layer civ parallax, authentic stone slab panel, submenu popouts, and soundtrack
- [x] Enhance In-Game Visuals (terrain multi-texture shader, dynamic water waves, ACES tonemapping, SSAO, soft shadows)
- [x] Rebuild C++ library with `scons platform=linux target=template_debug -j8`
- [x] Port 15 additional 0 A.D. maps to `data/maps/` and benchmark pathfinding (`tools/bench_pathfind.py`)
- [x] Commit and push all data, assets, code, and documentation to remote repository
