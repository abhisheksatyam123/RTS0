# Remaining Gaps — 0 A.D. parity by subsystem

This doc owns how close RTS0 is to 0 A.D.: the whole-game gaps (areas A–F) and the per-system G1–G10 table. The todo list is [docs/PLAN.md](PLAN.md), vendored-code porting is [docs/MIGRATION.md](MIGRATION.md), and asset counts are [docs/ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md). The 2026-08-29 per-gap audit bodies and Phase 0 checklist that used to follow the table are in [docs/archive/REMAINING_GAPS_2026-08-29_DETAIL.md](archive/REMAINING_GAPS_2026-08-29_DETAIL.md).

## Read this first: whole-game parity (2026-10-08)

The G1–G10 table below grades individual systems against 0 A.D.'s semantics, and `python3 tools/check_parity.py` reports 92% for them. Those scores are self-graded and only cover the systems listed. Judged by what a match actually plays like, RTS0 is roughly **a quarter of the way** to 0 A.D. (estimate from an 18-minute headless match against the AI plus a code read on 2026-10-08). It is a small two-player prototype with many of 0 A.D.'s systems in early form.

The game-level gaps below are each being built by its own parallel thread (areas A–F). Their order and status live in [PLAN.md](PLAN.md#game-level-work-areas-af); update the row here when an area lands.

| Area | Gap today | Where in code |
|---|---|---|
| A. Per-player economy | Landed 2026-10-08 (#7 and the command-ownership PR): 8 player slots each with their own stock, techs, phase, civ and team; lockstep commands run as the sending player and can only order what it owns; the HUD shows the local player. Left: diplomacy is still player 0's view only (no 8x8 matrix), combat ignores teams, fog of war is player 0's, and the starting-resources option sets player 0 only. | `PlayerState` in `simulation.h`, `TurnManager::player_for_client`, `filter_owned_units`, `main_node.cpp` `local_player()` |
| B. Game modes | Wave-defense mode is on in every match (a wave every 35 s, win by surviving wave 5). No skirmish/conquest victory options. | `tick_waves`, `check_victory_conditions`, `scenes/menu/gamesetup.gd` |
| C. AI (Petra) | Village-phase stall fixed 2026-10-08 (queues no longer hoard income, foundations get builders, the base is anchored on the real Town Center); the AI reaches the town phase in ~7 min with 40+ workers. Still one AI player only, and no farming yet, so food runs out on small maps. | `petra_*` in `simulation.cpp`, `petra_ai.h` |
| D. Civ rosters and heroes | Landed 2026-10-08 (#10, #12): all 14 civs including Han field their own 0 A.D. units and heroes with 0 A.D. stats, civ bonuses are data, and 36 of 65 hero auras are modelled. Left: the other 29 auras (loot, capture and similar effects), a train panel that lists roster units by name, and Petra pricing from the roster. | `tools/extract_0ad_rosters.py`, `civ_roster_data.h`, `spawn_unit`, `cmd_train_template` |
| E. Buildings, techs, farming | 26 building types with 0 A.D. stats and phase locks, and fields (endless grain, 5 farmers, diminishing returns) as of PR #11. 83 0 A.D. techs researched at their own buildings, with prerequisites, civ limits and 0 A.D.'s phase rules (5 Village / 3 Town structures). Not applied yet: trade gain, healing, arrow count, train/research time and unit-unlock techs (no champion rosters). Player 0 still gets a flat +2 food/s; stables and ranges don't train anything yet (area D picks rosters). | building/tech tables in `sim_types.h`, `cmd_build`, `cmd_research`, `tick_gather`, `tick_trickle` |
| F. Maps and scale | Random maps run at every 0 A.D. size (128 Tiny to 512 Giant) with nav, FoW and territory grids that scale at 2-unit cells (PR #14). 21 random map types, including six modelled on 0 A.D.'s Mainland, Alpine Lakes, Lake, Gulf of Bothnia, Ardennes Forest and Near-Eastern Badlands. Left: a Map Size option in game setup, map size in multiplayer setup, and more of 0 A.D.'s ~60 random maps. | `rms_generator.cpp`, `generate_random_map`, `rebuild_nav_grid`, `tick_fow` |

Two-real-client multiplayer is tracked in PLAN.md P1. In good shape today: pathfinding, territory, capture, garrisons, formations, trade, auras, promotion, naval transport and much of 0 A.D.'s art.

The G1–G10 system table was last refreshed 2026-10-06 (Wave 1+2).

## How to read this doc

- **Fidelity %** = share of 0 A.D. semantics actually ported (0% = not started, 100% = diff-clean).
- **Blocking** = `YES` if it blocks deterministic parity / competitive play / network lockstep.
- **Owner** = suggested code owner / team.
- `RTS0 Location:line` refs are `src/...` unless noted; line numbers are approximate anchors as of the refresh date.
- When work lands, update its row here in the same PR as the code, and its line in `docs/PLAN.md`.

---

## G1-G10 Summary Table

| ID | Subsystem | 0AD Source Files | RTS0 Location:line | Fidelity % | Blocking | Owner |
|---|---|---|---|---|---|---|
| G1 | **Pathfinding** (octile A* + waypoints + navgrid) | `CCmpPathfinder.cpp` / `LongPathfinder.cpp:400` (octile `10/14`) / `HierarchicalPathfinder.h:353` / `Grid.h:412` | `src/simulation/grid_nav.h` `NavGrid::find_path`, `rebuild_regions`, ship `PASS_SHIP`, string-pull | **94%** | NO — JPS (`nav/long_pathfinder.h`, port of `LongPathfinder::ComputeJPSPath`) + reachability + cavalry slope live; `VertexPathfinder` short-range still open | Sim / Navigation |
| G2 | **Territory & FoW** (influence, 3x3 blur, decay, shroud) | `CCmpTerritoryManager.cpp` / `TerritoryManager.h` / `TerritoryDecay.js` / `Los.h` | `src/simulation/grid_nav.h` `blur_territory`, `src/simulation/simulation.cpp` `rebuild_territory`, `minimap_view.cpp` | **90%** | NO — influence flood + 3x3 blur + FoW shroud live | Sim / Gameplay |
| G3 | **Market / Barter / Trader** (dynamic prices, distance gains, civ trade mults) | `Market.js:42` `GetPrices()` pow supply fluctuation / `Barter.js` / `Trader.js:1` (`TradeRoute`, `Gains` per distance) | `src/simulation/simulation.cpp:1044` `pow(0.98f, deals)` pow-pricing, `:1064` `calculate_trader_gain` distance scaling + `trade_gain_mult` civ bonus, `baseSpread` barter fee | **100%** | NO — pow-based market pricing, trader civ mults, distance gains, barter fee + `cmd_barter` lockstep cmd 23 all landed; `check_parity.py --unit market` 8/8 (2026-10-06) | Sim / Economy |
| G4 | **Auras** (AuraManager, sum/max stacking, heroes, temples) | `Auras.js` (`AuraManager`, `ApplyAura`) / `Heal.js` / `Promotion.js` | `src/simulation/aura_manager.h`, `src/simulation/simulation.cpp` `rebuild_auras`, `tick_gather`, `tick_combat` | **90%** | NO — unified AuraManager live | Sim / Gameplay |
| G5 | **Formation + Combat fidelity** (offsets, heading rotation, speed tables, counters) | `Formation.js` (`GetFormationOffsets`) / `FormationAttack.js` / `Attack.js:GetAttackBonus` / `Resistance.js:84` / `CCmpRangeManager.cpp` | `src/simulation/simulation.cpp` `cmd_move` heading `atan2f` rotation, `formation_speed_mult`, `tick_combat`; `:3008` `counter_mul = 2.5f`, `:3030-3035` `pow(0.9f, armor)` hack/pierce/crush, `:3039-3042` elevation `height_diff` (+25% cap / -20% floor), `:3123`/`:3769` 4.5m siege splash at 60% quadratic falloff | **95%** | NO — full 13-formation enum (template_player.xml:84-96), `formation_metrics[13]`, combat checks `--unit formation` 9/9; combat fidelity ~95% (armor/counter/splash/elevation live; only spear-vs-cavalry arc-style bonus curves + unit-vs-structure multipliers remain) | Sim / Combat |
| G6 | **UI — Session / HUD / Selection / Production / Diplomacy** | `gui/session/` (`TopPanel.xml`, `PanelEntities.xml`, `unit_training.xml`, `diplomacy/`, `minimap/`) | `src/presentation/main_node.cpp` — inspector, queue, lobby panel, civ voice toggle | **98%** | NO — `docs/UI_PARITY.md` **11/11 complete** (2026-10-06); shift-queue waypoints, Ctrl+0-9 groups, wall-ghost lockstep build dispatch, chat addressee, shroud 3-state, flare, build hotkeys all live with code:line evidence; P4 widget extraction planned (`docs/PLAN.md:55-100`) | Presentation / UI |
| G7 | **Assets — Models / Textures / Animations / Audio** | `art/meshes/` ~4280 PMD + `art/animation/biped/*.dae` / `public.zip` | `assets/models/manifest.json` (counts in `docs/ASSET_MANIFEST_TODO.md`); Han civ batch registered in `tools/extract_0ad_assets.py` (`CIV_ART_FOLDERS` :591/:605) | **75%** | NO — unique unit/building silhouettes ~100% (manifest matrix); raw PMD coverage was **15.5% (663/4280)** at the 2026-10-06 refresh; **Han (Chinese) civ landed**: 14 unique unit meshes + 14/14 unique buildings, 10 unit fallbacks to generic (documented fallback matrix lives in the `tools/extract_0ad_assets.py` header, `# C2 HAN` block :16-81) | Art / Pipeline |
| G8 | **Network — TurnManager / Sync / Hash / Replay** | `NetClientTurnManager.cpp:188` / `TurnManager.h/.cpp` + `HashSerializer.cpp` | `src/network/turn_manager.cpp` 200ms turns, full SoA/building/stockpile FNV-1a hash, deterministic sort; diplo-via-TurnManager `post_diplomacy` (`turn_manager.h:98`), `apply_diplomacy()` turn-boundary write point (`:326-328`) | **98%** | NO — full SoA hash + `tools/test_replay.cpp` round-trip (negative control) + `test_diplo_net` PASS; landed: **start gate** `net_game_started_` (`main_node.h:185`), **join-sync** `NMT_JOIN_SYNC_STATE`=32 / `NMT_JOIN_SYNC_CHECK`=33 (`net_types.h:68`/`:71`), **fleet upkeep** command `NET_CMD_MAINTAIN_FLEET`=24 (`net_types.h:134`, generated at `turn_manager.cpp:544`, dispatched `:559`/`:861`); sim hook `Simulation::cmd_maintain_fleet` (`simulation.h:195`, `simulation.cpp:835`) LANDED | Network |
| G9 | **Network — Socket Transport / Protocol / Lobby** | `NetServer.cpp` / `NetClient.cpp` / `NetMessages.h` (`NMT_*`) | `NetHost` UDP + handshake auto-auth, `LobbyRoom`, `tools/lobby_server.py`, in-game lobby UI | **96%** | NO — headless 2-peer playtest PASS 3 seeds x 2000 turns + diplo UDP replication test; **NMT_GAME_START resend landed** (`net_host.cpp:466 sendGameStartTo`, gated on `game_start_sent_` `net_host.h:81-82`, `:175-177`); remaining: real 2-Godot-client session / NAT | Network |
| G10 | **Petra AI — Economy / Military / AttackPlan / Threat** | `ai/petra/` (33 modules) | queues, raid+assault, transport, opening, fishing, research pop-gate, expansion workers | **90%** | NO — per-sea fleet registry + PLAN_RAID dock targets landed (`--unit petra` 28/28); **maintainFleet upkeep generation+dispatch landed 2026-10-06** (`NET_CMD_MAINTAIN_FLEET`=24 `net_types.h:134`, generator `turn_manager.cpp:544`, dispatch `:559`/`:861`, sim hook `simulation.h:195`/`simulation.cpp:835`); remaining: per-sea wishlist registry on the Simulation side, more bases | AI |

> Fidelity notes: combat (counters, logarithmic armor, elevation, splash) is ~95% after the 2026-10-06 refresh below. All other gaps are polish items tracked in PLAN.md P1–P5. G7's raw 0 A.D. PMD coverage is a long tail without gameplay impact (every canonical unit, building and fauna has a unique model); the counts are in [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md).

---

## Wave 1+2 Parity Refresh — 2026-10-06

> **Dated refresh.** Numbers below supersede the 2026-08-29 stub audit for the G1–G10 **table only**;
> the historical body sections below are kept verbatim as an audit trail (per "How to read this doc").
> `python3 tools/check_parity.py` overall: **90% → 92%**.

| Gap | Before | After | What landed (evidence) |
|---|---|---|---|
| G3 Market / Barter / Trader | 80% (`check_parity`) / 95% (table) | **100%** / **100%** | Pow-based market pricing `simulation.cpp:1044` (`std::pow(0.98f, deals)`), `calculate_trader_gain` `:1064`, per-civ `trade_gain_mult`, `baseSpread` barter fee. `check_parity.py --unit market` **8/8 (100%)**. |
| G5 Formation + combat | 100% (formation wiring only) | **95% combined, combat ~95%** | Counter bonus `:3008` `counter_mul = 2.5f`; logarithmic armor `:3030-3035` `hack/pierce/crush * powf(0.9f, armor)`; elevation `:3039-3042` `height_diff` (+25% cap / -20% floor); siege splash `:3123` / projectile splash `:3769` (4.5m, 60%, quadratic `1 - d²/r²`). Remaining: unit-vs-structure damage multipliers + full counter-bonus arc. |
| G6 UI | 98% | **98% (UI_PARITY 11/11)** | Unchanged at 98% — the ceiling is the P4 widget extraction, not missing widgets. `docs/UI_PARITY.md:98` VERIFIED 11/11. |
| G7 Assets | 15% | **15.5% (663/4280)** | Manifest **663 models** / 1316 anims / 98 skels / 652 terrain. **Han (Chinese) civ landed**: `tools/extract_0ad_assets.py` `CIV_ART_FOLDERS` `:591`/`:605`; 14 unique unit meshes, **14/14 unique buildings, 0 structure fallbacks**; 10 unit fallbacks to generic. Full fallback matrix is documented in the extract script header `# C2 HAN` block (`:16-81`). |
| G8 Network TurnManager | 98% | **98%** (scope widened, % held) | Landed this wave: **diplo-via-TurnManager** (`turn_manager.h:98 post_diplomacy`, `:326-328 apply_diplomacy` turn-boundary write point); **start gate** (`main_node.h:185 net_game_started_`); **join-sync** `NMT_JOIN_SYNC_STATE`=32 / `NMT_JOIN_SYNC_CHECK`=33 (`net_types.h:68`/`:71`); **fleet upkeep** `NET_CMD_MAINTAIN_FLEET`=24 (`net_types.h:134`), generated `turn_manager.cpp:544`, dispatched `:559`/`:861`. Sim hook `Simulation::cmd_maintain_fleet` (`simulation.h:195`, `simulation.cpp:835`) **landed**. % held at 98% because the real 2-Godot-client session gate is still open. |
| G9 Network Lobby | 96% | **96%** | **NMT_GAME_START resend landed** — `net_host.cpp:466 sendGameStartTo` (one-peer late-join resend), gated on `game_start_sent_` (`net_host.h:81-82`, `:175-177`). Remaining: real 2-Godot-client session / NAT. |
| `check_parity.py` overall | 90% | **92%** | Only G3 moved in the 12-bucket table (80→100 = +20 over 12 buckets = +1.67pp), recomputed by the unchanged formula `int(sum(v)/len(v))` at `tools/check_parity.py`. G7 stayed 15% (663/4280 floors to 15), so no drift there. |

### Re-verified facts (grep, 2026-10-06)

- `simulation.cpp`: `calculate_trader_gain` @1064 · `pow(0.9` @3030 · `counter_mul = 2.5f` @3008 · `splash` @1943/3123/3768/4228 · `height_diff` @2790/2944/3039.
- `python3 -c "import json; print(len(json.load(open('assets/models/manifest.json'))['models']))"` → **663**.
- `docs/UI_PARITY.md:98` → **11/11 complete**.
- P4 session-HUD modularization plan exists at **`docs/PLAN.md:55-100`** (target widget file list + 6-phase extraction order 4.1-4.6). NOTE: the handoff brief cited `PLAN.md:129-183`; the shipped file is only 114 lines, so the actual range is recorded here.
- E-hooks `main_node.cpp:3743-3745` (host broadcast setup) / `:3803-3810` (joiner consumes `NMT_GAME_START`).
- F-hook `simulation.h:195` / `simulation.cpp:835` (`cmd_maintain_fleet`).
- B-net `NET_CMD_MAINTAIN_FLEET = 24` (`net_types.h:134`), `NMT_JOIN_SYNC_STATE = 32` / `NMT_JOIN_SYNC_CHECK = 33` (`net_types.h:68`/`:71`).

---

## Verification Commands

Run these after each Phase 0-5 gap closure. They are the canonical `scons` + `build.py` commands requested.

```bash
# 1. Native GDExtension (requires godot-cpp at godot-cpp/ branch 4.3)
# See SConstruct:28 — src/0ad/ is reference only, not built.
scons target=template_debug -j8
scons target=template_release -j8

# 2. Cross-platform Godot export driver (tools/build.py)
# Builds GDExtension first (via scons), then Godot headless import + export.
# Env: GODOT=/path/to/Godot_v4.3-stable_linux.x86_64
python3 tools/build.py linux --skip-scons   # if scons already ran
python3 tools/build.py linux                # full (scons + godot)
python3 tools/build.py all                  # windows+linux

# 3. Parity check (Phase 0)
python3 tools/check_parity.py --unit nav --seed 42 --verbose
python3 tools/check_parity.py --unit territory --seed 42
python3 tools/check_parity.py --unit market --trades 10
python3 tools/check_parity.py --unit auras --range 10,14,16,18
python3 tools/check_parity.py --unit formation --count 12 --type phalanx

# 4. Quick sim smoke (headless, no Godot)
# If you add a native test harness:
# ./bin/librtso.linux.template_debug.so --test-sim --ticks 1000 --seed 42

# 5. Asset bake (if you change art pipeline)
python3 tools/extract_0ad_assets.py --help
python3 tools/build_assets.py

# 6. Lint / diff vs 0 A.D.
diff -u ../0ad/source/simulation2/helpers/Grid.h src/simulation/grid_nav.h | head -n 100
diff -u ../0ad/source/simulation2/helpers/HierarchicalPathfinder.h src/simulation/grid_nav.h | head -n 100
```

> Note: `scons` compiles `src/` but skips `src/0ad/` (see `SConstruct:28` `if "0ad" in root.split(os.sep): continue`). This is intentional — vendor is reference + future porting, not linked today. Native SoA in `src/simulation/simulation.cpp:4194` collapses `31 CCmp* + 84 JS` components into a single `Simulation` class; `src/0ad/` stays as `diff -u` reference.

## Fidelity scale (for new gaps)

- `0%` not started
- `1-10%` stub / hard-coded
- `11-30%` partial math
- `31-50%` usable but not 1:1
- `51-80%` close
- `81-100%` parity
