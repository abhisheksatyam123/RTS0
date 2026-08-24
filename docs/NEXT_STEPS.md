# Next Steps (prioritized)

## P0 — Make it run (do this before anything else)
1. Open the project in Godot 4.3 .NET, let it restore/build, press **F5**.
   - Nothing has been compiled yet; expect a handful of small C# fixes on first build.
   - Commit the fixes immediately (`git add -A && git commit`).
2. Verify CI goes green on GitHub after pushing those fixes.
3. One local desktop export end-to-end (`docs/BUILD.md`) → proves the toolchain.

## P1 — Close the game loop (Thronefall-style MVP)
4. **A\* pathfinding** on the sim grid in `GameSim` (deterministic, lockstep-friendly).
5. **Unit state machine**: `Idle / Move / Gather / Return / Attack`.
6. **Resources**: trees/gold nodes as sim entities; villagers gather → deposit at center.
7. **Combat**: HP, attack range/cooldown, auto-aggro, death + respawn of views.
8. **Enemy waves**: spawn at map edge every N sec, march to your center → win/lose screen.
   → After this you have a playable game worth showing anyone.

## P2 — Multiplayer + mobile
9. Netcode M2/M3: host/join UI, ENet transport, host applies remote commands, snapshot sync (`docs/MULTIPLAYER.md`).
10. Touch input scheme feeding the same command pipeline (tap select, pinch zoom).
11. Real Android device testing weekly.

## P3 — Look & feel
12. Art pass: low-poly asset packs (Kenney/Quaternius/Synty), Thronefall palette, fog/glow polish.
13. Juice: animations, sounds, minimap, health bars, wave banners.

## P4 — Scale & ship
14. Perf milestones from `docs/PERFORMANCE.md`: staggered AI ticks, off-thread sim, compute shaders if needed.
15. Release engineering: version stamping from git tags, release keystore, Play Console listing, TestFlight, itch.io/Steam page.
