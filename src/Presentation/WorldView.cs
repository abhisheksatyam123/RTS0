using System.Collections.Generic;
using Godot;
using Rts0.Core;

namespace Rts0.Presentation;

// Presentation layer: reads sim state, never mutates it.
// GPU: 6 MultiMesh batches (player/enemy units + center/house + tree/gold) → 6 draw calls + 1 terrain.
// Thronefall shaders: flat ramp + outline, wind for foliage, territory tint. Frustum culling via Frustum.
// Deterministic sim stays on Fixed/Grid/Pathfinder; this layer only lerps and draws.
public partial class WorldView : Node3D
{
    private const int InitialCapacity = 256;

    private MultiMeshInstance3D _playerMmi;
    private MultiMesh _playerMm;
    private MultiMeshInstance3D _enemyMmi;
    private MultiMesh _enemyMm;
    private int _capPlayer, _capEnemy;

    // GPU-batched buildings/resources (was per-Node3D, now MultiMesh)
    private MultiMeshInstance3D _centerMmi;
    private MultiMesh _centerMm;
    private int _capCenter;
    private MultiMeshInstance3D _houseMmi;
    private MultiMesh _houseMm;
    private int _capHouse;
    private MultiMeshInstance3D _treeMmi;
    private MultiMesh _treeMm;
    private int _capTree;
    private MultiMeshInstance3D _goldMmi;
    private MultiMesh _goldMm;
    private int _capGold;

    private MeshInstance3D _terrain;
    private readonly Dictionary<int, Node3D> _buildingViews = new(); // fallback for non-batched (debug)
    private readonly Dictionary<int, Node3D> _resourceViews = new();
    private readonly Dictionary<int, Node3D> _activeRings = new();
    private readonly Stack<Node3D> _ringPool = new();

    private ShaderMaterial _unitMat;
    private ShaderMaterial _terrainMat;
    private ShaderMaterial _foliageMat;

    public override void _Ready()
    {
        // thronefall shaders — load or fallback to StandardMaterial3D if missing
        _unitMat = LoadShaderMat("res://shaders/unit_thronefall.gdshader");
        _foliageMat = LoadShaderMat("res://shaders/foliage_wind.gdshader");
        _terrainMat = LoadShaderMat("res://shaders/terrain.gdshader");

        _capPlayer = _capEnemy = _capCenter = _capHouse = _capTree = _capGold = InitialCapacity;
        _playerMm = MakeUnitMultiMesh(_unitMat, new Color(0.36f, 0.62f, 0.94f), 0.0f);
        _enemyMm = MakeUnitMultiMesh(_unitMat, new Color(0.92f, 0.32f, 0.28f), 1.0f);
        _centerMm = MakeBuildingMultiMesh(_unitMat, new Vector3(4, 4, 4), false);
        _houseMm = MakeBuildingMultiMesh(_unitMat, new Vector3(1.8f, 1.8f, 1.8f), false);
        _treeMm = MakeResourceMultiMesh(_foliageMat, ResourceType.Tree);
        _goldMm = MakeResourceMultiMesh(_unitMat, ResourceType.GoldVein);

        _playerMmi = new MultiMeshInstance3D { Multimesh = _playerMm };
        _enemyMmi = new MultiMeshInstance3D { Multimesh = _enemyMm };
        _centerMmi = new MultiMeshInstance3D { Multimesh = _centerMm };
        _houseMmi = new MultiMeshInstance3D { Multimesh = _houseMm };
        _treeMmi = new MultiMeshInstance3D { Multimesh = _treeMm };
        _goldMmi = new MultiMeshInstance3D { Multimesh = _goldMm };
        AddChild(_playerMmi); AddChild(_enemyMmi);
        AddChild(_centerMmi); AddChild(_houseMmi);
        AddChild(_treeMmi); AddChild(_goldMmi);

        _terrain = CreateTerrain();
        AddChild(_terrain);
    }

    private static ShaderMaterial LoadShaderMat(string path)
    {
        if (ResourceLoader.Exists(path))
        {
            var sh = GD.Load<Shader>(path);
            if (sh != null) return new ShaderMaterial { Shader = sh };
        }
        return null;
    }

    private static MultiMesh MakeUnitMultiMesh(ShaderMaterial shaderMat, Color color, float team)
    {
        var mm = new MultiMesh
        {
            TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = new CapsuleMesh { Radius = 0.4f, Height = 1.2f },
            InstanceCount = InitialCapacity,
            VisibleInstanceCount = 0,
            UseCustomData = true,
        };
        Material mat;
        if (shaderMat != null)
        {
            mat = (ShaderMaterial)shaderMat.Duplicate();
            // team encoded via shader param + per-instance custom
            mm.Mesh.SurfaceSetMaterial(0, mat);
        }
        else
        {
            mat = new StandardMaterial3D { AlbedoColor = color, Roughness = 0.7f };
            mm.Mesh.SurfaceSetMaterial(0, mat);
        }
        return mm;
    }

    private static MultiMesh MakeBuildingMultiMesh(ShaderMaterial mat, Vector3 size, bool isCenter)
    {
        var mm = new MultiMesh
        {
            TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = new BoxMesh { Size = size },
            InstanceCount = InitialCapacity,
            VisibleInstanceCount = 0,
            UseCustomData = true,
        };
        if (mat != null) mm.Mesh.SurfaceSetMaterial(0, (ShaderMaterial)mat.Duplicate());
        else mm.Mesh.SurfaceSetMaterial(0, new StandardMaterial3D { AlbedoColor = new Color(0.72f, 0.64f, 0.52f), Roughness = 0.9f });
        return mm;
    }

    private static MultiMesh MakeResourceMultiMesh(ShaderMaterial mat, ResourceType type)
    {
        Mesh mesh = type == ResourceType.Tree
            ? (Mesh)new CylinderMesh { TopRadius = 0.18f, BottomRadius = 0.22f, Height = 1.8f }
            : new BoxMesh { Size = new Vector3(1.2f, 0.8f, 1.2f) };
        var mm = new MultiMesh
        {
            TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = mesh,
            InstanceCount = InitialCapacity,
            VisibleInstanceCount = 0,
            UseCustomData = true,
        };
        if (mat != null) mm.Mesh.SurfaceSetMaterial(0, (ShaderMaterial)mat.Duplicate());
        else mm.Mesh.SurfaceSetMaterial(0, new StandardMaterial3D { AlbedoColor = type == ResourceType.Tree ? new Color(0.18f, 0.55f, 0.22f) : new Color(0.92f, 0.78f, 0.18f) });
        return mm;
    }

    private MeshInstance3D CreateTerrain()
    {
        var plane = new PlaneMesh { Size = new Vector2(GameSim.MapHalfExtent * 2, GameSim.MapHalfExtent * 2), SubdivideWidth = 32, SubdivideDepth = 32 };
        var mat = _terrainMat ?? (Material)new StandardMaterial3D { AlbedoColor = new Color(0.55f, 0.72f, 0.42f), Roughness = 0.95f };
        plane.Material = mat;
        return new MeshInstance3D { Mesh = plane, Position = Vector3.Zero };
    }

    public void Sync(GameSim sim, float alpha)
    {
        // Frustum cull helpers (CPU side, GPU does rest via no-draw when outside)
        var cam = GetViewport()?.GetCamera3D();
        var frustum = cam != null ? BuildFrustum(cam) : null;

        // Units — split by team into two GPU batches + INSTANCE_CUSTOM (x=team, y=hp)
        int nPlayer = 0, nEnemy = 0;
        foreach (var u in sim.Units) if (u.Team == Team.Player) nPlayer++; else nEnemy++;
        EnsureCapacity(ref _playerMm, ref _capPlayer, nPlayer);
        EnsureCapacity(ref _enemyMm, ref _capEnemy, nEnemy);

        int iP = 0, iE = 0;
        foreach (SimUnit unit in sim.Units)
        {
            Vector3 pos = unit.PrevPos.Lerp(unit.Pos, alpha) + new Vector3(0, 0.6f, 0);
            if (frustum != null && !frustum.IsSphereVisible(new Vector3D(pos.X, pos.Y, pos.Z), 0.6f)) continue;
            var xf = new Transform3D(Basis.Identity, pos);
            float hpFrac = unit.MaxHp > 0 ? (float)unit.Hp / unit.MaxHp : 1f;
            var custom = new Color(unit.Team == Team.Player ? 0f : 1f, hpFrac, 0f, 0f);
            if (unit.Team == Team.Player) { _playerMm.SetInstanceTransform(iP, xf); _playerMm.SetInstanceCustomData(iP, custom); iP++; }
            else { _enemyMm.SetInstanceTransform(iE, xf); _enemyMm.SetInstanceCustomData(iE, custom); iE++; }
        }
        _playerMm.VisibleInstanceCount = iP;
        _enemyMm.VisibleInstanceCount = iE;

        // Buildings — GPU batched (center vs house)
        int nCenter = 0, nHouse = 0;
        foreach (var b in sim.Buildings) if (b.IsCenter) nCenter++; else nHouse++;
        EnsureCapacity(ref _centerMm, ref _capCenter, nCenter);
        EnsureCapacity(ref _houseMm, ref _capHouse, nHouse);
        int iC = 0, iH = 0;
        foreach (SimBuilding b in sim.Buildings)
        {
            Vector3 pos = b.BasePos + new Vector3(b.SizeMeters * 0.5f, b.SizeMeters * 0.5f, b.SizeMeters * 0.5f);
            if (frustum != null && !frustum.IsBoxVisible(new Vector3D(pos.X, pos.Y, pos.Z), new BoundingBoxAligned(new Vector3D(-b.SizeMeters*0.5f,-b.SizeMeters*0.5f,-b.SizeMeters*0.5f), new Vector3D(b.SizeMeters*0.5f,b.SizeMeters*0.5f,b.SizeMeters*0.5f)))) continue;
            var xf = new Transform3D(Basis.Identity, pos);
            var custom = new Color(b.Team == Team.Player ? 0f : 1f, (float)b.Hp / b.MaxHp, 0f, 0f);
            if (b.IsCenter) { _centerMm.SetInstanceTransform(iC, xf); _centerMm.SetInstanceCustomData(iC, custom); iC++; }
            else { _houseMm.SetInstanceTransform(iH, xf); _houseMm.SetInstanceCustomData(iH, custom); iH++; }
        }
        _centerMm.VisibleInstanceCount = iC;
        _houseMm.VisibleInstanceCount = iH;

        // Resources — GPU batched (tree vs gold, foliage wind via INSTANCE_CUSTOM.z)
        int nTree = 0, nGold = 0;
        foreach (var r in sim.Resources) if (r.Type == ResourceType.Tree) nTree++; else nGold++;
        EnsureCapacity(ref _treeMm, ref _capTree, nTree);
        EnsureCapacity(ref _goldMm, ref _capGold, nGold);
        int iT = 0, iG = 0;
        foreach (SimResource r in sim.Resources)
        {
            Vector3 pos = r.Pos;
            if (frustum != null && !frustum.IsSphereVisible(new Vector3D(pos.X, pos.Y, pos.Z), 0.8f)) continue;
            var xf = new Transform3D(Basis.Identity, pos);
            var custom = new Color(0, 1, r.Type == ResourceType.Tree ? 1f : 0f, 0); // z=wind flag
            if (r.Type == ResourceType.Tree) { _treeMm.SetInstanceTransform(iT, xf); _treeMm.SetInstanceCustomData(iT, custom); iT++; }
            else { _goldMm.SetInstanceTransform(iG, xf); _goldMm.SetInstanceCustomData(iG, custom); iG++; }
        }
        _treeMm.VisibleInstanceCount = iT;
        _goldMm.VisibleInstanceCount = iG;

        // legacy per-node fallback removed — now fully batched; rings stay per-unit (few)
        foreach (var kv in _activeRings)
        {
            if (!sim.UnitById.TryGetValue(kv.Key, out var unit)) continue;
            kv.Value.Position = unit.PrevPos.Lerp(unit.Pos, alpha);
        }
    }

    private static Rts0.Core.Frustum BuildFrustum(Camera3D cam)
    {
        try
        {
            var frustum = new Rts0.Core.Frustum();
            var planes = cam.GetFrustum(); // Godot.Collections.Array<Godot.Plane>
            int n = planes.Count;
            frustum.SetNumPlanes(n);
            for (int i = 0; i < n && i < Rts0.Core.Frustum.MaxPlanes; i++)
            {
                Godot.Plane gp = planes[i];
                var nrm = new Vector3D(gp.Normal.X, gp.Normal.Y, gp.Normal.Z);
                var pt = nrm * gp.D;
                var p = new Rts0.Core.Plane();
                p.Set(nrm, pt);
                p.Normalize();
                frustum[i] = p;
            }
            return frustum;
        }
        catch { return null; }
    }

    private static void EnsureCapacity(ref MultiMesh mm, ref int cap, int need)
    {
        if (need <= cap) return;
        cap = need * 2;
        mm.InstanceCount = cap;
    }

    public void SetSelected(int unitId, bool selected)
    {
        if (selected)
        {
            if (_activeRings.ContainsKey(unitId)) return;
            var ring = _ringPool.Count > 0 ? _ringPool.Pop() : CreateRing();
            ring.Visible = true;
            AddChild(ring);
            _activeRings[unitId] = ring;
        }
        else
        {
            if (!_activeRings.Remove(unitId, out var ring)) return;
            ring.Visible = false;
            RemoveChild(ring);
            _ringPool.Push(ring);
        }
    }

    private static Node3D CreateRing()
    {
        var mat = new StandardMaterial3D { AlbedoColor = new Color(0.3f, 0.9f, 1.0f), ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded };
        return new MeshInstance3D { Name = "Ring", Mesh = new TorusMesh { InnerRadius = 0.42f, OuterRadius = 0.58f, Material = mat }, Position = new Vector3(0, 0.06f, 0) };
    }

    private static Node3D CreateBuildingVisual(SimBuilding b)
    {
        float size = b.SizeMeters;
        var root = new Node3D { Position = b.BasePos };
        var bodyMat = new StandardMaterial3D { AlbedoColor = b.Team == Team.Player ? (b.IsCenter ? new Color(0.85f, 0.74f, 0.55f) : new Color(0.72f, 0.64f, 0.52f)) : new Color(0.6f, 0.3f, 0.3f), Roughness = 0.9f };
        root.AddChild(new MeshInstance3D { Mesh = new BoxMesh { Size = new Vector3(size, size, size), Material = bodyMat }, Position = new Vector3(size * 0.5f, size * 0.5f, size * 0.5f) });
        if (b.IsCenter)
        {
            var roofMat = new StandardMaterial3D { AlbedoColor = new Color(0.55f, 0.32f, 0.26f) };
            root.AddChild(new MeshInstance3D { Mesh = new PrismMesh { Size = new Vector3(size * 1.15f, size * 0.5f, size * 1.15f), Material = roofMat }, Position = new Vector3(size * 0.5f, size * 0.75f + size * 0.5f, size * 0.5f) });
        }
        return root;
    }

    private static Node3D CreateResourceVisual(SimResource r)
    {
        var root = new Node3D { Position = r.Pos };
        if (r.Type == ResourceType.Tree)
        {
            var trunkMat = new StandardMaterial3D { AlbedoColor = new Color(0.42f, 0.28f, 0.18f) };
            var leavesMat = new StandardMaterial3D { AlbedoColor = new Color(0.18f, 0.55f, 0.22f) };
            root.AddChild(new MeshInstance3D { Mesh = new CylinderMesh { TopRadius = 0.18f, BottomRadius = 0.22f, Height = 1.8f, Material = trunkMat }, Position = new Vector3(0, 0.9f, 0) });
            root.AddChild(new MeshInstance3D { Mesh = new SphereMesh { Radius = 0.7f, Height = 1.0f, Material = leavesMat }, Position = new Vector3(0, 1.6f, 0) });
        }
        else
        {
            var goldMat = new StandardMaterial3D { AlbedoColor = new Color(0.92f, 0.78f, 0.18f), Roughness = 0.4f, Metallic = 0.6f };
            root.AddChild(new MeshInstance3D { Mesh = new BoxMesh { Size = new Vector3(1.2f, 0.8f, 1.2f), Material = goldMat }, Position = new Vector3(0, 0.4f, 0) });
        }
        return root;
    }
}
