# Remaining Gaps — 0 A.D. parity by subsystem

This doc owns how close RTS0 is to 0 A.D.: the whole-game gaps (areas A–F) and the per-system G1–G10 table. The todo list is [docs/PLAN.md](PLAN.md), vendored-code porting is [docs/MIGRATION.md](MIGRATION.md), and asset counts are [docs/ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md). The 2026-08-29 per-gap audit bodies and Phase 0 checklist that used to follow the table are in [docs/archive/REMAINING_GAPS_2026-08-29_DETAIL.md](archive/REMAINING_GAPS_2026-08-29_DETAIL.md).

## Read this first: whole-game parity (refreshed 2026-10-08, afternoon)

The G1–G10 table below grades individual systems against 0 A.D.'s semantics, and `python3 tools/check_parity.py` reports 92% for them. Those scores are self-graded and only cover the systems listed. Judged by what a match actually plays like, RTS0 is now **a little over half way** to 0 A.D., up from about a quarter this morning. That is an estimate from a code read and a 20-minute headless match against the AI on main at `4413a5c`, after today's area PRs landed (per-player economy and diplomacy, game modes, one Petra per AI slot with farming and the city phase, civ rosters and team bonuses, buildings, techs and fields, map sizes and 33 random maps, building steering).

What moved it: every civ now fields 0 A.D.'s own units (367 unit templates across 14 civs, Han included, with 0 A.D. stats, heroes and team bonuses). There are 26 building types, 83 of 0 A.D.'s technologies with phase rules, and working fields. Every player has its own diplomacy row, and combat honours teams. Each AI slot runs its own Petra. Maps come in all 0 A.D. sizes with 33 random map types. In the benchmark the AI reaches the town phase at about 3 to 4 minutes and the city phase at about 6, and has 68 workers and 242 units at 20 minutes.

What still separates it from a 0 A.D. match, in rough order of impact:

1. **Setup options 0 A.D. has and RTS0 doesn't:** population cap (RTS0 has no cap at all; 0 A.D. defaults to 300), ceasefire, revealed map and nomad starts (`scenes/menu/gamesetup.gd` header lists them as missing sim setters). Allied victory is also still missing.
2. **Multiplayer between two real game clients** is still untested outside the headless harness (PLAN.md P1).
3. **Content breadth:** 33 of 0 A.D.'s ~60 random maps, no scenario or skirmish maps, and 29 of 65 hero auras not yet modelled.

Each area below is built by its own parallel thread. Their order and status live in [PLAN.md](PLAN.md#game-level-work-areas-af); update the row here when an area lands.

| Area | Gap today | Where in code |
|---|---|---|
| A. Per-player economy | Landed 2026-10-08 (#7, #17 and the diplomacy PR): 8 player slots each with their own stock, techs, phase, civ and team; lockstep commands run as the sending player and can only order what it owns; the HUD shows the local player; every player keeps its own stance row in an 8x8 diplomacy matrix, combat/capture/splash/shared vision go through `is_hostile`/`is_ally` so teams and alliances are honoured, and the starting-resources option reaches every slot. Nothing left for area A; per-player fog of war is area F's and landed in #20/#24. | `PlayerState`, `diplomacy_[8][8]`, `is_hostile`/`is_ally`/`apply_team_diplomacy` in `simulation.h`, `TurnManager::player_for_client`, `filter_owned_units`, `main_node.cpp` `local_player()` |
| B. Game modes | Mostly closed (2026-10-08): wave defense is off unless ticked in Game Setup; all seven 0 A.D. victory conditions and the map size are selectable and sent to multiplayer peers. Allied victory, Last Man Standing, population limit and ceasefire are in, and each human sees victory or defeat from their own seat. Defeated players, player 0 included, observe with the map revealed while the match goes on until one side (or one player under Last Man Standing) is left. Revealed and explored map, wonder and relic hold times and relic count are setup options. Left: a defeated player's leftover entities stay instead of turning Gaia, and 0 A.D.'s Nomad starts, treasures toggle and regicide garrison option. | `tick_waves`, `check_victory_conditions`, `scenes/menu/gamesetup.gd` |
| C. AI (Petra) | Stall fixed 2026-10-08: town phase ~3 min and city phase ~6-7 min with 40-60 workers, fields once hunt and berries run low. Each seated AI slot runs its own Petra and human slots get none; Petra picks targets with `is_hostile`, so AIs on one team don't fight. Petra has no free income any more: like 0 A.D., difficulty scales its gather and trade rate and its build, train and research times (`InitGame.js` "AI Bonus"). Petra's diplomacy works toward every player (0 A.D. diplomacyManager.js): it answers an offer to ally or go neutral by accepting, declining, or asking for a tribute, answers a declaration of war in kind, and under Last Man Standing turns on its strongest remaining ally once no enemy is left. It tributes any ally short of a resource it has plenty of, and accepts tribute for an alliance. Left: Petra does not seek allied markets for the international trade bonus. | `petra_*` in `simulation.cpp`, `petra_ai.h` |
| D. Civ rosters and heroes | Landed 2026-10-08 (#10, #12, #21 and the team-bonus PR): all 14 civs including Han field their own 0 A.D. units and heroes with 0 A.D. stats, civ bonuses are data and reach every building, 36 of 65 hero auras are modelled, and every civ's team bonus applies to its player and allies (all 14; Han Silk Road raises allies' international trade bonus since the trade PR). Players can train every roster unit and hero by name, gated by 0 A.D.'s phase requirements. Siege towers (5 civs) are in the roster and fire their own arrows; packable stone throwers and bolt shooters carry ROSTER_PACKABLE for the packing port. Champions wait for the unlock technology their own 0 A.D. template names (23 rows). Left: the other 29 auras (loot, capture and similar effects), siege tower garrison arrows and transport, and Petra pricing from the roster and team bonuses. | `tools/extract_0ad_rosters.py`, `civ_roster_data.h`, `spawn_unit`, `cmd_train_template` |
| E. Buildings, techs, farming | 26 building types with 0 A.D. stats and phase locks, and fields (endless grain, 5 farmers, diminishing returns) as of PR #11. 83 0 A.D. techs researched at their own buildings, with prerequisites, civ limits and 0 A.D.'s phase rules (5 Village / 3 Town structures), and every effect RTS0 models is applied: gather rates, carry capacity, attack, armour, health, speed, range, structure health/attack/range/build time, house population, training and research time, trade gain, healing range and rate, structure arrow counts, and the champion and house-civilian unlocks. Player 0's free food and wood income and the Town Center trickle are removed (0 A.D. has neither). Sentry towers upgrade to defense towers and walls to gates as in 0 A.D.'s Upgrade.js (cost, time, phase, cancel with refund, health fraction and garrison carried over), and structures placed after a health tech get it. Gates work as in 0 A.D.'s Gate.js: long-range paths cross an unlocked gate, it opens while the owner's or an ally's unit is near and won't close on a unit in the opening, enemies are stopped at it, and the owner can lock it shut. Left: units can't carry a garrison (siege towers, rams, ships), the civ-only upgrades that need new structure types (Han Imperial Court and great tower, Carthaginian apartment, Persian tachara, Germanic encampment), and techs 0 A.D. has that RTS0 has no mechanic for (garrison and territory effects, loot, promotion experience) are not extracted. | building/tech tables in `sim_types.h`, `cmd_build`, `cmd_research`, `tick_gather`, `tick_trickle` |
| F. Maps and scale | Random maps run at every 0 A.D. size (128 Tiny to 512 Giant) with nav, FoW and territory grids that scale at 2-unit cells (PR #14). 39 random map types, 24 of them modelled on 0 A.D. random map scripts (listed in 0AD_ATTRIBUTION.md). 35 fixed 0 A.D. skirmish and scenario maps load from `data/maps/`. Terrain textures follow the map's biome (14 biomes, 0 A.D. textures). Game setup has a Map Size option that multiplayer games use too. A multiplayer joiner sees its own fog and minimap. Left: more of 0 A.D.'s ~60 random maps; per-tile terrain painting and biome-specific trees and animals. | `rms_generator.cpp`, `generate_random_map`, `rebuild_nav_grid`, `tick_fow` |

**Benchmark baseline** on main at `4413a5c` (`python3 tools/bench_match.py`, seed 42, continental, Tiny, Endless, 20 minutes; timings from one cloud machine, so only compare against runs on the same machine): tick mean 1.24 ms, p50 1.14 ms, p95 2.64 ms, p99 3.68 ms, max 6.4 ms; AI end state phase 3, 68 workers, 242 units, 73 buildings. The tick cost doubled from the morning's 0.61 ms mean because the AI now fields about twice as many units.

Two-real-client multiplayer is tracked in PLAN.md P1. In good shape today: pathfinding (including VertexPathfinder steering around buildings), territory, capture, garrisons, formations, trade, auras, promotion, naval transport and much of 0 A.D.'s art.

The G1–G10 system table was last refreshed 2026-10-06 (Wave 1+2), except the G1 and G10 rows, updated 2026-10-08.

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
| G1 | **Pathfinding** (octile A* + waypoints + navgrid) | `CCmpPathfinder.cpp` / `LongPathfinder.cpp:400` (octile `10/14`) / `HierarchicalPathfinder.h:353` / `Grid.h:412` | `src/simulation/grid_nav.h` `NavGrid::find_path`, `rebuild_regions`, ship `PASS_SHIP`, string-pull | **94%** | NO — JPS (`nav/long_pathfinder.h`, port of `LongPathfinder::ComputeJPSPath`) + reachability + cavalry slope live; `VertexPathfinder` steers land units around buildings (`nav/building_steering.h`, #8); units are not yet short-path obstructions (MIGRATION.md todo 2) | Sim / Navigation |
| G2 | **Territory & FoW** (influence, 3x3 blur, decay, shroud) | `CCmpTerritoryManager.cpp` / `TerritoryManager.h` / `TerritoryDecay.js` / `Los.h` | `src/simulation/grid_nav.h` `blur_territory`, `src/simulation/simulation.cpp` `rebuild_territory`, `minimap_view.cpp` | **90%** | NO — influence flood + 3x3 blur + FoW shroud live | Sim / Gameplay |
| G3 | **Market / Barter / Trader / Tribute** (barter prices, distance gains, international bonus, tribute) | `Barter.js` / `Market.js` `CalculateTraderGain` / `Trader.js` / `globalscripts/Trade.js` / `Player.js` `TributeResource` | `src/simulation/simulation.cpp` `cmd_barter`, `tick_barter`, `calculate_trade_gain`, trader routes in the movement tick, `cmd_tribute` | **95%** | NO — ported 2026-10-08 (tribute/barter/trade PR): Barter.js prices (sell 90 / buy 110 plus a shared difference that moves 2% per 100 and drifts back 0.5% every 5 s, deals of 100 or 500, needs an owned Market); Trade.js gain (grows with distance squared, normalised by map size, trader GainMultiplier 0.75, Persian +25%, Handicraft techs +15% each); Market.js international bonus (20% to each market owner when the two markets' owners differ, Han Silk Road +20% for allies); traders trade at own, allied or neutral markets but not enemy ones, carry random goods (25% each) and are paid on arrival; tribute between any two players as lockstep cmd 27. Left: merchant ships and dock trade, garrisoned-trader bonus, Petra trade between allies | Sim / Economy |
| G4 | **Auras** (AuraManager, sum/max stacking, heroes, temples) | `Auras.js` (`AuraManager`, `ApplyAura`) / `Heal.js` / `Promotion.js` | `src/simulation/aura_manager.h`, `src/simulation/simulation.cpp` `rebuild_auras`, `tick_gather`, `tick_combat` | **90%** | NO — unified AuraManager live | Sim / Gameplay |
| G5 | **Formation + Combat fidelity** (offsets, heading rotation, speed tables, counters) | `Formation.js` (`GetFormationOffsets`) / `FormationAttack.js` / `Attack.js:GetAttackBonus` / `Resistance.js:84` / `CCmpRangeManager.cpp` | `src/simulation/simulation.cpp` `cmd_move` heading `atan2f` rotation, `formation_speed_mult`, `tick_combat`; `:3008` `counter_mul = 2.5f`, `:3030-3035` `pow(0.9f, armor)` hack/pierce/crush, `:3039-3042` elevation `height_diff` (+25% cap / -20% floor), `:3123`/`:3769` 4.5m siege splash at 60% quadratic falloff | **95%** | NO — full 13-formation enum (template_player.xml:84-96), `formation_metrics[13]`, combat checks `--unit formation` 9/9; combat fidelity ~95% (armor/counter/splash/elevation live; only spear-vs-cavalry arc-style bonus curves + unit-vs-structure multipliers remain) | Sim / Combat |
| G6 | **UI — Session / HUD / Selection / Production / Diplomacy** | `gui/session/` (`TopPanel.xml`, `PanelEntities.xml`, `unit_training.xml`, `diplomacy/`, `minimap/`) | `src/presentation/main_node.cpp` — inspector, queue, lobby panel, civ voice toggle | **98%** | NO — `docs/UI_PARITY.md` **11/11 complete** (2026-10-06); shift-queue waypoints, Ctrl+0-9 groups, wall-ghost lockstep build dispatch, chat addressee, shroud 3-state, flare, build hotkeys all live with code:line evidence; P4 widget extraction planned (`docs/PLAN.md:55-100`) | Presentation / UI |
| G7 | **Assets — Models / Textures / Animations / Audio** | `art/meshes/` ~4280 PMD + `art/animation/biped/*.dae` / `public.zip` | `assets/models/manifest.json` (counts in `docs/ASSET_MANIFEST_TODO.md`); Han civ batch registered in `tools/extract_0ad_assets.py` (`CIV_ART_FOLDERS` :591/:605) | **75%** | NO — unique unit/building silhouettes ~100% (manifest matrix); raw PMD coverage was **15.5% (663/4280)** at the 2026-10-06 refresh; **Han (Chinese) civ landed**: 14 unique unit meshes + 14/14 unique buildings, 10 unit fallbacks to generic (documented fallback matrix lives in the `tools/extract_0ad_assets.py` header, `# C2 HAN` block :16-81) | Art / Pipeline |
| G8 | **Network — TurnManager / Sync / Hash / Replay** | `NetClientTurnManager.cpp:188` / `TurnManager.h/.cpp` + `HashSerializer.cpp` | `src/network/turn_manager.cpp` 200ms turns, full SoA/building/stockpile FNV-1a hash, deterministic sort; diplomacy as lockstep cmd 22 only (the `NMT_DIPLOMACY` carrier is retired) | **98%** | NO — full SoA hash + `tools/test_replay.cpp` round-trip (negative control) + `test_diplo_net` PASS; landed: **start gate** `net_game_started_` (`main_node.h:185`), **join-sync** `NMT_JOIN_SYNC_STATE`=32 / `NMT_JOIN_SYNC_CHECK`=33 (`net_types.h:68`/`:71`), **fleet upkeep** command `NET_CMD_MAINTAIN_FLEET`=24 (`net_types.h:134`, generated at `turn_manager.cpp:544`, dispatched `:559`/`:861`); sim hook `Simulation::cmd_maintain_fleet` (`simulation.h:195`, `simulation.cpp:835`) LANDED | Network |
| G9 | **Network — Socket Transport / Protocol / Lobby** | `NetServer.cpp` / `NetClient.cpp` / `NetMessages.h` (`NMT_*`) | `NetHost` UDP + handshake auto-auth, `LobbyRoom`, `tools/lobby_server.py`, in-game lobby UI | **96%** | NO — headless 2-peer playtest PASS 3 seeds x 2000 turns + diplo UDP replication test; **NMT_GAME_START resend landed** (`net_host.cpp:466 sendGameStartTo`, gated on `game_start_sent_` `net_host.h:81-82`, `:175-177`); remaining: real 2-Godot-client session / NAT | Network |
| G10 | **Petra AI — Economy / Military / AttackPlan / Threat** | `ai/petra/` (33 modules) | queues, raid+assault, transport, opening, fishing, research pop-gate, expansion workers | **90%** | NO — per-sea fleet registry + PLAN_RAID dock targets landed (`--unit petra` 28/28); **maintainFleet upkeep generation+dispatch landed 2026-10-06** (`NET_CMD_MAINTAIN_FLEET`=24 `net_types.h:134`, generator `turn_manager.cpp:544`, dispatch `:559`/`:861`, sim hook `simulation.h:195`/`simulation.cpp:835`); stall fixed and one Petra per AI slot with fields and the city phase (2026-10-08, #16, #26); remaining: Petra diplomacy and tribute only with player 0, per-sea wishlist registry on the Simulation side, more bases | AI |

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
| G8 Network TurnManager | 98% | **98%** (scope widened, % held) | Landed this wave: **diplomacy as lockstep cmd 22 only** (the `NMT_DIPLOMACY` carrier and `post_diplomacy` queue were retired 2026-10-08: they scheduled by the receiver's own turn and trusted a wire actor byte); **start gate** (`main_node.h:185 net_game_started_`); **join-sync** `NMT_JOIN_SYNC_STATE`=32 / `NMT_JOIN_SYNC_CHECK`=33 (`net_types.h:68`/`:71`); **fleet upkeep** `NET_CMD_MAINTAIN_FLEET`=24 (`net_types.h:134`), generated `turn_manager.cpp:544`, dispatched `:559`/`:861`. Sim hook `Simulation::cmd_maintain_fleet` (`simulation.h:195`, `simulation.cpp:835`) **landed**. % held at 98% because the real 2-Godot-client session gate is still open. |
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
