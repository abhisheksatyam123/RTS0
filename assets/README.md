# Assets — RTS0 (0 A.D. baked + Thronefall fallback)

Runtime visuals are **0 A.D. art baked into GLB** (see `tools/extract_0ad_assets.py`) with
procedural primitives as fallback when a GLB is missing. Shaders stay Thronefall-style
where useful; baked models use imported `StandardMaterial3D`.

## Folder layout

```
assets/
  models/
    units/villager.glb     athenians/female_citizen (f_dress + head, player blue)
    units/swordsman.glb    athenians/infantry_swordsman_c (armor + helmet + shield + xiphos, enemy red)
    buildings/town_center.glb  athenians/civil_centre (structural/athen_cc_struct, 5.2m, ao baked)
    buildings/house.glb    hellenes/house variant D (hele_house_d, 2.1m, ao baked)
    vegetation/tree.glb    flora/oak_new (bark + canopy, alpha-mask leaves, 4.5m)
    vegetation/gold_vein.glb  geology/metal_granite_greek (stonemine, tiny boulders for carry)
    manifest.json          sources, baked size_meters, upstream material per asset
  textures/
    terrain/grass.png      types/aegean_anatolia/grass_01.cached.dds -> png 512
    terrain/dirt.png       types/aegean_anatolia/dirt_01.cached.dds
    terrain/rock.png       types/aegean_anatolia/cliff_01.cached.dds
    terrain/splat.png      Procedural.GenerateSplat() fallback (unused when tiles present)
    noise/terrain_noise.png  FastNoiseLite 512, used by terrain.gdshader fallback
  materials/thronefall/
    unit.tres / terrain.tres / foliage.tres   ShaderMaterial -> shaders/*.gdshader
  music/theme.ogg          audio/music/In_the_Shadow_of_Olympus.ogg (ambient loop)
         victory.ogg       You_are_Victorious!.ogg
         defeat.ogg        Dried_Tears.ogg
  imported/                **gitignored** drop zone for CC0 packs (Kenney/Quaternius)
```

## Pipeline (`tools/extract_0ad_assets.py`)

```
0ad repo ../0ad/binaries/data/mods/public/public.zip
  art/meshes/*.dae.cached.pmd  binary meshes (source/graphics/ModelDef.cpp, PSMD, CW front)
  art/actors/*.xml              variant groups + shared variants + prop attach points
  art/textures/skins/*.cached.dds  DXT1/5 -> PNG (fixed G6 channel bug)

-> parse PMD (versions 1-5, bind-pose already), compose actor trees,
   bake player-color (alpha-mask: shaders/glsl/model_common.fs `mix(playerColor,white,tex.a)`)
   + AO multiply, downscale >1024 to 512, flip winding (CW -> CCW), center+ground normalize
-> emit single-file GLB (embedded PNG) per id + manifest.json
```

Regenerate after pulling 0ad:

```bash
python3 tools/extract_0ad_assets.py              # --zip ../0ad/.../public.zip --out assets
# Validate (needs blender snap):
blender --background --python /tmp/opencode/render_cycles.py -- /tmp/previews
```

Import settings (auto on export): `filter=false, fix_handedness`, `msaa_3d=2`, textures `s3tc_bptc` desktop / `etc2_astc` Android.

## Fallback

`WorldView.cs:Make*MultiMesh` checks `Assets.IsRealModel(path)` — if a GLB is missing or
`ResourceLoader` fails (headless CI), it falls back to `Procedural` Capsule/Box/Cylinder
so the game always builds. Real models keep imported `StandardMaterial3D`; fallback uses
`unit_thronefall.gdshader` + `INSTANCE_CUSTOM` team/hp/wind.

## Scale

Targets baked into GLB (manifest `size_meters`): villager 1.7m, swordsman 1.8m,
town_center 5.2m (footprint 4), house 2.1m (footprint 1.8), tree 4.5m, gold_vein 1.8m.
`WorldView.Sync` uses `_real*` flags to pick Y offset: real units 0.02 vs 0.6 (capsule),
real buildings 0 vs size/2 (Box center).

## License

Art + music from 0 A.D. are **CC-BY-SA 3.0** (see `docs/0AD_ATTRIBUTION.md`). Keep
the GPL header on ported code files (`src/Core/**`). Thronefall shaders + glue are
yours, but shipped binary is GPL-2.0+ combined work.
