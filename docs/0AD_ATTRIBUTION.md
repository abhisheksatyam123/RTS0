# 0 A.D. Attribution & License

This project (RTS0) reuses algorithms and code directly from **0 A.D.** (https://play0ad.com)
by Wildfire Games, which is licensed under **GPL-2.0-or-later**.

## Files derived from 0 A.D.

| RTS0 file | 0 A.D. source | Lines | License |
|---|---|---|---|
| `src/Core/Maths/Fixed.cs` | `source/maths/Fixed.h` + `FixedVector2D.h` + `source/maths/Sqrt.cpp` | ~408 + 250 + 48 | GPL-2.0+ |
| `src/Core/Maths/Grid.cs` | `source/simulation2/helpers/Grid.h` | 412 | GPL-2.0+ |
| `src/Core/Pathfinding/*` | `source/simulation2/helpers/PathGoal.h` + `HierarchicalPathfinder.*` + `LongPathfinder.*` + `VertexPathfinder` | ~1300 | GPL-2.0+ |
| `src/Core/Simulation/Components/UnitAI.cs` | `binaries/data/mods/public/simulation/components/UnitAI.js` + `CCmpUnitMotion` | 6652 (condensed) | GPL-2.0+ |
| `tools/extract_0ad_assets.py` | `source/graphics/ModelDef.cpp` + `source/ps/FileIo.cpp` + `shaders/glsl/model_common.fs` (player-color alpha) | ~650 | GPL-2.0+ |

### Art & audio (CC-BY-SA 3.0, Wildfire Games)

| RTS0 asset | 0 A.D. source | Notes |
|---|---|---|
| `assets/models/units/villager.glb` | `art/actors/units/athenians/female_citizen.xml` → `skeletal/new/f_dress.dae` + `props/units/heads/...` | player blue baked (`player_trans_norm_spec`) |
| `assets/models/units/swordsman.glb` | `art/actors/units/athenians/infantry_swordsman_c.xml` → armor + helmet + `xiphos` + `aspis` | enemy red baked |
| `assets/models/buildings/town_center.glb` | `art/actors/structures/athenians/civil_centre.xml` → `structural/athen_cc_struct` + props | `no_trans_ao_parallax`, ao baked |
| `assets/models/buildings/house.glb` | `art/actors/structures/hellenes/house.xml` (variant D) → `hele_house_d` + `shields/nature/decor` | `no_trans_ao_parallax`, ao baked |
| `assets/models/vegetation/tree.glb` | `art/actors/flora/trees/oak_new.xml` → `gaia/tree_oak_new_a/c_top_a` | `basic_trans_wind`, alpha-mask canopy |
| `assets/models/vegetation/gold_vein.glb` | `art/actors/geology/metalmine_granite_greek.xml` → `gaia/stonemine_granite_a` | rock formation for carry anim |
| `assets/textures/terrain/grass.png` | `art/textures/terrain/types/aegean_anatolia/grass_01` | 512 tiled |
| `assets/textures/terrain/dirt.png` | `.../dirt_01` | |
| `assets/textures/terrain/rock.png` | `.../cliff_01` | |
| `assets/music/theme.ogg` | `audio/music/In_the_Shadow_of_Olympus.ogg` | ambient loop |
| `assets/music/victory.ogg` | `audio/music/You_are_Victorious!.ogg` | stinger |
| `assets/music/defeat.ogg` | `audio/music/Dried_Tears.ogg` | |

Sources manifest: `assets/models/manifest.json` (hash of each `actor/mesh/skin` ref).

All ported files retain the original GPL header:

> `Copyright (C) 2025 Wildfire Games. 0 A.D. is free software under GPL-2.0-or-later.`

## What this means for RTS0

* The **combined work** (RTS0 + ported 0 A.D. code) must be distributed under GPL-2.0-or-later
  if you distribute binaries, per GPL §2(b). This supersedes the original MIT LICENSE for
  the ported portions; the project's LICENSE file should be updated to GPL-2.0+ before the
  first public release beyond private development.

* Thronefall-style art, original Godot glue (`Main.cs`, `WorldView.cs`, `CameraRig.cs`),
  build scripts and docs remain your own, but as part of the combined binary they fall
  under GPL when you ship.

* Keep the GPL headers intact when you copy further files. When you reach a store release,
  include a copy of GPL-2.0 text and source offer (GitHub repo URL satisfies this).

## Build-reference

During porting, 0 A.D. was checked out at `../0ad` (commit from Aug 2025). To diff
or pull updates:

```bash
diff -u ../0ad/source/maths/Fixed.h scripts/core/Fixed.cs
```

## Alternatives if you need MIT/closed-source

If you must ship MIT/proprietary, replace the GPL-derived files with clean-room
reimplementations that only reuse *ideas* (not expression). The docs in
`docs/MULTIPLAYER.md` and `docs/PERFORMANCE.md` already describe the ideas in
non-GPL terms.
