# 0 A.D. Attribution & License

This project (RTS0) reuses algorithms and code directly from **0 A.D.** (https://play0ad.com)
by Wildfire Games, which is licensed under **GPL-2.0-or-later**.

## Files derived from 0 A.D.

| RTS0 file | 0 A.D. source | Lines | License |
|---|---|---|---|
| `scripts/core/Fixed.cs` | `source/maths/Fixed.h` + `FixedVector2D.h` + `source/maths/Sqrt.cpp` | ~408 + 250 + 48 | GPL-2.0+ |
| `scripts/core/Grid.cs` | `source/simulation2/helpers/Grid.h` | 412 | GPL-2.0+ |
| `scripts/core/Pathfinder.cs` | `source/simulation2/helpers/PathGoal.h` + `HierarchicalPathfinder.*` + `LongPathfinder.*` | 84 + 353/865 + 1170 | GPL-2.0+ |
| `scripts/core/UnitAI.cs` | `binaries/data/mods/public/simulation/components/UnitAI.js` + `CCmpUnitMotion` | 6652 (condensed) | GPL-2.0+ |

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
