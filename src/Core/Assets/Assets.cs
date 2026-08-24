// Runtime asset loader — mirrors 0ad art/ loader but with Godot + fallback.
// Thronefall palette, CC0, procedural fallback: if GLB missing, WorldView uses primitives.
using Godot;

namespace Rts0.Core.Assets;

public static class Assets
{
    public const string UnitShader = "res://shaders/unit_thronefall.gdshader";
    public const string TerrainShader = "res://shaders/terrain.gdshader";
    public const string FoliageShader = "res://shaders/foliage_wind.gdshader";

    public const string UnitMaterial = "res://assets/materials/thronefall/unit.tres";
    public const string TerrainMaterial = "res://assets/materials/thronefall/terrain.tres";
    public const string FoliageMaterial = "res://assets/materials/thronefall/foliage.tres";
    public const string TerrainNoise = "res://assets/textures/noise/terrain_noise.png";

    // Canonical model paths (if imported GLB exists, load; else fallback to primitives in WorldView)
    public static string UnitModel(string id) => $"res://assets/models/units/{id}.glb";
    public static string BuildingModel(string id) => $"res://assets/models/buildings/{id}.glb";
    public static string VegetationModel(string id) => $"res://assets/models/vegetation/{id}.glb";

    public static bool ResExists(string path) => ResourceLoader.Exists(path);

    public static Mesh TryLoadMesh(string path, Mesh fallback)
    {
        if (!ResExists(path)) return fallback;
        var res = GD.Load<PackedScene>(path);
        if (res != null)
        {
            var inst = res.Instantiate();
            var mi = FindMeshInstance(inst);
            if (mi?.Mesh != null) return mi.Mesh;
        }
        var mesh = GD.Load<Mesh>(path);
        return mesh ?? fallback;
    }

    public static ShaderMaterial LoadUnitMaterial()
        => TryLoadMat(UnitMaterial, UnitShader);

    public static ShaderMaterial LoadTerrainMaterial()
        => TryLoadMat(TerrainMaterial, TerrainShader);

    public static ShaderMaterial LoadFoliageMaterial()
        => TryLoadMat(FoliageMaterial, FoliageShader);

    private static ShaderMaterial TryLoadMat(string tres, string shaderFallback)
    {
        if (ResExists(tres))
        {
            var m = GD.Load<ShaderMaterial>(tres);
            if (m != null) return m;
        }
        if (ResExists(shaderFallback))
        {
            var sh = GD.Load<Shader>(shaderFallback);
            if (sh != null) return new ShaderMaterial { Shader = sh };
        }
        return null;
    }

    private static MeshInstance3D FindMeshInstance(Node n)
    {
        if (n is MeshInstance3D mi) return mi;
        foreach (Node c in n.GetChildren())
        {
            var f = FindMeshInstance(c);
            if (f != null) return f;
        }
        return null;
    }
}
