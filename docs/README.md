# RTS0 docs

Each doc owns one topic. When a fact belongs to another doc's topic, link to that doc instead of repeating it, so there is only one place to update and nothing drifts out of sync.

## Where things live

| Topic | Owner doc | Notes |
|---|---|---|
| What to work on next, in order | [PLAN.md](PLAN.md) | The one ordered todo list. Area docs below may keep a detailed todo for their area; PLAN.md links to them. |
| How close RTS0 is to 0 A.D. (whole-game areas A–F, per-system G1–G10) | [REMAINING_GAPS.md](REMAINING_GAPS.md) | The only place fidelity percentages live. |
| Vendored 0 A.D. C++ in `src/0ad/`: what's ported, what's left | [MIGRATION.md](MIGRATION.md) | Updated by every PR that ports vendored code. |
| 0 A.D. domain → RTS0 code map | [0AD_TO_RTS0_FEATURE_MAP.md](0AD_TO_RTS0_FEATURE_MAP.md) | Where each 0 A.D. feature lives in RTS0. |
| Code layout and invariants | [ARCHITECTURE.md](ARCHITECTURE.md) | |
| Building, exporting, CI | [BUILD.md](BUILD.md) | |
| Netcode model, multiplayer rules, playtest harnesses | [MULTIPLAYER.md](MULTIPLAYER.md) | Code-level notes: [src/network/README.md](../src/network/README.md). |
| Performance strategy and benchmarks | [PERFORMANCE.md](PERFORMANCE.md) | |
| CPU & GPU utilization profiling & scaling roadmap | [CPU_GPU_UTILIZATION_REPORT.md](CPU_GPU_UTILIZATION_REPORT.md) | Multi-core CPU & Vulkan GPU profiling across 150–7,600 units, 5-track optimization roadmap |
| Session HUD parity with 0 A.D. `gui/session` | [UI_PARITY.md](UI_PARITY.md) | |
| Petra AI parity with 0 A.D. `ai/petra` | [AI_PETRA.md](AI_PETRA.md) | |
| Asset coverage and asset todo | [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md) | The only place asset counts live. |
| How 0 A.D. art maps onto RTS0 (textures, UVs, skeletons, clips) | [ASSET_MAPPING.md](ASSET_MAPPING.md) | Running the extractor: [assets/README.md](../assets/README.md). |
| Licensing, and which files came from 0 A.D. | [0AD_ATTRIBUTION.md](0AD_ATTRIBUTION.md) | Code is GPL-2.0-or-later (`LICENSE`), 0 A.D. art/audio/maps are CC BY-SA 3.0 (`assets/LICENSE`). Every new copy or port of 0 A.D. code gets a row there, and so does 0 A.D. data landing in a new folder. |
| Old checklists and audits | [archive/](archive/README.md) | History only; don't plan from them. |

`NEXT_STEPS.md` is a pointer to PLAN.md, kept so old links still work.

## Rules for editing docs

1. **One fact, one home.** Before adding a number, status or todo, check the table above. If another doc owns it, update that doc and link to it.
2. **Update in the same PR.** A PR that lands work updates its line in PLAN.md and, if it changes parity, its row in the REMAINING_GAPS.md table. A PR that ports vendored code updates MIGRATION.md, and any PR that copies or ports 0 A.D. code follows the rules in 0AD_ATTRIBUTION.md.
3. **Replace, don't annotate.** When something becomes untrue, change or delete it. Don't add "superseded" notes on top of stale text; git history keeps the old version, and longer records can move to `archive/`.
4. **Date the numbers.** Counts and percentages say when they were measured and with which command.
