## Tasks
- [x] T1 [lead] Audit and expand animation/scaling coverage across all model categories in model_library.cpp vs 0 A.D.
- [x] T2 [lead] Implement per-unit animation phase desynchronization in MultiMesh crowd instancer via instance custom data time offset
- [x] T3 [lead] Implement projected terrain decals for selection rings (Godot Decal node replacing flat mesh quad)
- [x] T4 [lead] Rebuild with scons platform=linux target=template_debug -j4 and verify with run_game.sh + parity checks
- [x] T5 [lead] Fix chicken slaughter carcass turning into berry bush (filter RES_SUB_MEAT from generic fruit bush MultiMesh instancer)
- [x] T6 [lead] Fix units sliding while in sitting or gather/build/attack animation (speed-check guards in resolve_unit_anim)
- [x] T7 [lead] Fix close-distance pathfinding and unit-to-unit pushing stuck behavior (nudge overlap in moving_push, ensure slaughter/gather_meat runtime clips)

## Systems
- Repository: RTS0 = 0 A.D. native port (C++ SoA sim + Godot 4.3 GDExtension).
- Model Scaling: expanded target_height categories across all unit types, siege weapons, ships, buildings, flora, and fauna in src/graphics/assets/model_library.cpp matching 0 A.D. dimensions.
- Animation Crowd Instancer: 3-phase desynchronization (CROWD_PHASES = 3) in CrowdBatch and cycle_puppet_meshes so units playing identical clips do not animate in exact lockstep.
- Selection Rings: projected terrain decals using Godot Decal nodes (unit_ring_decals_ pool) with DECAL_LAYER cull mask, normal fade, and dynamic tint/modulation for selected/highlighted units, livestock, and resources without clipping on steep slopes.
- Fauna Slaughter & Carcass: 0 A.D. Health.js CreateCorpse spawns meat supplies from killed fauna. Filtered RES_SUB_MEAT out of fruit_bush MultiMesh instancer in `src/presentation/world_view_node.cpp` so killed chickens and animals retain their dead carcass visuals instead of turning into berry bushes.
- Animation Sliding Fix: Checked `speed2 > 0.05f` in `resolve_unit_anim` for `ACTION_GATHER`, `ACTION_BUILD`, and `ACTION_ATTACK` so units that begin moving transition to walking/running animations rather than sliding in a sitting or ground-strike posture. Added `gather_meat` and `slaughter` PSA registration in `ensure_runtime_clips`.
- Collision Separation & Pushing: Moving units nudge against idle units if their collision radiuses overlap (`r_overlap`), preventing moving units from getting stuck against stationary units.
- Tests & Verification:
  - SCons build: scons platform=linux target=template_debug -j4 -> 100% SUCCESS
  - Parity tests: tools/check_parity.py --unit petra (37/37 100% PASS)
  - Netcode tests: tools/check_parity.py --unit netcode (17/17 100% PASS) and ./bin/test_netcode (100% PASS)
  - Asset audit: tools/audit_assets.py (165/165 unique paths 0 missing, 100% PASS)

## Log
- 2026-10-18 [lead] Session started: read model_library.cpp target_height/load, world_view_node crowd instancer + ring placement, todo.md updated with new tasks.
- 2026-10-18 [lead] T1: Audited and expanded target_height in model_library.cpp for all units, siege, ships, buildings, flora, and fauna.
- 2026-10-18 [lead] T2: Implemented 3-phase animation desynchronization in MultiMesh crowd instancer.
- 2026-10-18 [lead] T3: Replaced flat TorusMesh quad selection rings with projected terrain Decal nodes for units, fauna, and resources.
- 2026-10-18 [lead] T4: Rebuilt engine with scons; verified test suites.
- 2026-10-18 [lead] T5-T7: Fixed chicken slaughter carcass turning into berry bush; fixed units sliding in sitting/harvest posture by gating action animations with speed check; fixed close-distance pushing overlap between moving and stationary units; registered gather_meat and slaughter animations in ensure_runtime_clips.
