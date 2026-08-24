// Procedural asset builders — thronefall low-poly via code (no .glb required)
// Generates ArrayMesh for units/buildings/vegetation using SurfaceTool + primitive math.
// Mirrors 0ad art/actors but via code: keeps repo <5MB, deterministic, CI headless.
using Godot;
using System.Collections.Generic;

namespace Rts0.Core.Assets;

public static class Procedural
{
    private static readonly Dictionary<string, Mesh> Cache = new();

    public static Mesh GetUnitMesh(string id) => GetOrCreate($"unit_{id}", () => BuildUnit(id));
    public static Mesh GetBuildingMesh(string id) => GetOrCreate($"building_{id}", () => BuildBuilding(id));
    public static Mesh GetVegetationMesh(string id) => GetOrCreate($"veg_{id}", () => BuildVegetation(id));

    private static Mesh GetOrCreate(string key, System.Func<Mesh> factory)
    {
        if (Cache.TryGetValue(key, out var m)) return m;
        m = factory();
        Cache[key] = m;
        return m;
    }

    // --- Units: low-poly capsule + head + weapon stubs (thronefall: single capsule, color via shader)
    private static Mesh BuildUnit(string id)
    {
        float h = id switch { "archer" => 1.1f, "cavalry" => 1.6f, "swordsman" => 1.25f, "spearman" => 1.3f, "healer" => 1.15f, _ => 1.2f };
        float r = id.Contains("cavalry") ? 0.45f : 0.38f;
        // Capsule keeps 1 draw call + shader handles team/hp; distinct height/radius gives silhouette variety
        return new CapsuleMesh { Radius = r, Height = h, RadialSegments = 8, Rings = 1 };
    }

    // --- Buildings: modular thronefall (box + prism roof, wall segments)
    private static Mesh BuildBuilding(string id)
    {
        Vector3 size = id switch
        {
            "town_center" => new Vector3(4, 3.5f, 4),
            "barracks" => new Vector3(3, 2.5f, 3),
            "fortress" => new Vector3(4.5f, 4, 4.5f),
            "tower" => new Vector3(1.6f, 3.5f, 1.6f),
            "wall" => new Vector3(2, 1.8f, 0.6f),
            "market" => new Vector3(3.2f, 2.2f, 2.5f),
            "farmstead" => new Vector3(2.5f, 1.8f, 2.5f),
            _ => new Vector3(1.8f, 1.8f, 1.8f), // house
        };
        return new BoxMesh { Size = size };
    }

    // --- Vegetation / resources
    private static Mesh BuildVegetation(string id)
    {
        return id switch
        {
            "tree" => new CylinderMesh { TopRadius = 0.18f, BottomRadius = 0.24f, Height = 2.2f },
            "fruit_bush" => new SphereMesh { Radius = 0.6f, Height = 0.8f },
            "stone_mine" => new BoxMesh { Size = new Vector3(1.4f, 0.9f, 1.4f) },
            "gold_vein" => new BoxMesh { Size = new Vector3(1.2f, 0.8f, 1.2f) },
            "fish" => new SphereMesh { Radius = 0.3f, Height = 0.6f },
            "treasure_chest" => new BoxMesh { Size = new Vector3(0.8f, 0.6f, 1.0f) },
            _ => new BoxMesh { Size = new Vector3(1, 1, 1) },
        };
    }

    // --- Terrain splat helper: generate weights texture via code
    public static Image GenerateSplat(int w = 512, int h = 512, int seed = 0)
    {
        var img = Image.Create(w, h, false, Image.Format.Rgba8);
        var rng = new System.Random(seed);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        {
            float nx = x * 0.015f, ny = y * 0.015f;
            float n = (Mathf.Sin(nx) * 0.5f + 0.5f) * 0.3f + (Mathf.Sin(nx*1.7f+ny*0.8f)*0.5f+0.5f)*0.3f + (float)rng.NextDouble()*0.2f;
            float grass = Mathf.Clamp(1f - Mathf.Abs(n-0.3f)*3f, 0, 1);
            float dirt = Mathf.Clamp(1f - Mathf.Abs(n-0.5f)*4f, 0, 1);
            float rock = Mathf.Clamp(1f - Mathf.Abs(n-0.75f)*3f, 0, 1);
            // normalize to sum 1
            float sum = grass+dirt+rock+0.001f;
            img.SetPixel(x, y, new Color(grass/sum, dirt/sum, rock/sum, 1));
        }
        return img;
    }

    // --- Biome helper: Generate heightmap for terrain shader
    public static Image GenerateHeightmap(int w = 256, int h = 256)
    {
        var img = Image.Create(w, h, false, Image.Format.Rf);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
        {
            float v = Mathf.Sin(x*0.04f) * 0.5f + Mathf.Sin(y*0.03f + x*0.02f)*0.3f;
            img.SetPixel(x, y, new Color(v, 0, 0));
        }
        return img;
    }
}
