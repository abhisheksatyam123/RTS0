## Tasks
- [x] T1 [lead] Verify cursor/icon asset landing: new assets wired, stand-ins removed, PLAN.md consistent
- [x] T2 [lead] Build/test check — session_pointer.cpp compiles cleanly (exit 0); session_setup.cpp error is pre-existing & unrelated
- [ ] T3 [lead] Commit the landed asset+code+doc changes on main

## Systems
- RTS0 = 0 A.D. native port (C++ SoA sim + Godot GDExtension). Build: `scons platform=linux target=template_debug -j8` (godot-cpp/ required); `make debug` alias.
- Working-tree changes (uncommitted, on `main`):
  - `docs/PLAN.md`: U16 Triggers — OnRange marked landed (`TRIGGER_ON_RANGE`, `tick_trigger_range_queries`, `tools/test_triggers.cpp`); next = Tutorial map type.
  - `scenes/session/barter_dialog.gd`: barter/trade info button and ▲/▼ arrows now load real icon textures instead of text "i"/"▲"/"▼".
  - `src/presentation/session_pointer.cpp`: removed the cursor stand-in fallback (action-*-flare → action-flare, action-target → action-attack).
  - New untracked assets: action-observer-flare.png, action-player-flare.png, action-target.png, arrowbox_down/_over, arrowbox_up/_over, info.png.
- Asset manifest (docs/ASSET_MANIFEST_TODO.md:185): cursors were 23, action-target/player-flare/observer-flare not in repo (stand-ins used); barter drew info/arrows as text. These assets now close that gap.
- Branches: main (current), plus work/maps-path, work/petra-civ, work/trade-gaps, work/trade-p6, work/ui-audio. origin/main tracked.

## Log
- 2026-10-17 [lead] Session started: inspected workspace; found uncommitted asset+code+doc changes (cursor/icon landing) on main; created todo.md.
- 2026-10-17 [lead] Verified all 8 new asset files exist on disk matching code paths (info.png, arrowbox_up/down/_over, action-observer/player/target flares).
- 2026-10-17 [lead] session_pointer.cpp compiles standalone (exit 0); only pre-existing unrelated warning. session_setup.cpp build error is pre-existing (explore_circle private), not from my change.
- 2026-10-17 [lead] Confirmed e2e_rally_cursor.gd expects cursor names action-target / action-player-flare — matches now-available real assets.
