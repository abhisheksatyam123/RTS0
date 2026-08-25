# Next Steps (prioritized)

## P0 — Make it run (do this before anything else)
1. Open the project in Godot 4.3 .NET, let it restore/build, press **F5**.
   - Nothing has been compiled yet; expect a handful of small C# fixes on first build.
   - Commit the fixes immediately (`git add -A && git commit`).
2. Verify CI goes green on GitHub after pushing those fixes.
3. One local desktop export end-to-end (`docs/BUILD.md`) → proves the toolchain.

## P1 — Close the game loop (Thronefall-style MVP)
4. **A\* pathfinding** on the sim grid in `GameSim` (deterministic, lockstep-friendly). ✓ `LongPathfinder` + `HierarchicalPathfinder` ported, `GameSim.RebuildPassability` wired.
5. **Unit state machine**: `Idle / Move / Gather / Return / Attack`. ✓ `UnitAI` FSM from 0ad, stances, carry anim stubs.
6. **Resources**: trees/gold nodes as sim entities; villagers gather → deposit at center. ✓ `ResourceGatherer/ResourceSupply/Dropsite`, 4 scattered nodes.
7. **Combat**: HP, attack range/cooldown, auto-aggro, death + respawn of views. ✓ `Health/Attack/RangeManager`, projectiles omitted (melee).
8. **Enemy waves**: spawn at map edge every N sec, march to your center → win/lose screen. ✓ `GameSim Tick` waves, `EndGame`.
   → Playable MVP done (press F5; WASD/QE, LMB drag select, RMB move/gather/attack, B build).
   → **0 A.D. art bake done 2026-04:** `tools/extract_0ad_assets.py` + GLBs + `WorldView` real-model path + music (see `assets/README.md`, `docs/0AD_ATTRIBUTION.md`).

## P2 — Multiplayer + mobile
9. Netcode M2/M3: host/join UI, ENet transport, host applies remote commands, snapshot sync (`docs/MULTIPLAYER.md`).
10. Touch input scheme feeding the same command pipeline (tap select, pinch zoom).
11. Real Android device testing weekly — `export_presets.cfg` ready, CI `workflow_dispatch android:true`.

## P3 — Look & feel
12. Art pass: ✓ done via 0 A.D. bake (player blue `0.36,0.62,0.94` / enemy red `0.92,0.32,0.28` baked, throttle: buildings 5.2/2.1m, units 1.7/1.8m). Thronefall shaders remain fallback.
13. Juice: animations (skeletal .psa → glTF not yet; T-pose static), sounds (theme loop; UI/attack/gather sfx todo), minimap (`SubViewport`) todo, health bars / wave banners todo, terrain splat wiring (grass/dirt/rock PNGs present, shader sampling todo).

## P4 — Scale & ship
14. Perf milestones from `docs/PERFORMANCE.md`: staggered AI ticks, off-thread sim, compute shaders if needed.
15. Release engineering: version stamping from git tags, release keystore, Play Console listing, TestFlight, itch.io/Steam page.
