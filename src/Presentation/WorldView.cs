using System.Collections.Generic;
using Godot;
using Rts0.Core;

namespace Rts0.Presentation;

// Presentation layer: reads sim state, never mutates it.
// Units render through two MultiMeshInstance3D (player blue / enemy red) -> 2 draw calls.
// Resources (trees / gold veins) are few, rendered as individual MeshInstances.
// Deterministic sim stays on Fixed/Grid/Pathfinder; this layer only lerps and draws.
public partial class WorldView : Node3D
{
    private const int InitialCapacity = 256;

    private MultiMeshInstance3D _playerMmi;
    private MultiMesh _playerMm;
    private MultiMeshInstance3D _enemyMmi;
    private MultiMesh _enemyMm;
    private int _capPlayer, _capEnemy;

    private readonly Dictionary<int, Node3D> _buildingViews = new();
    private readonly Dictionary<int, Node3D> _resourceViews = new();
    private readonly Dictionary<int, Node3D> _activeRings = new();
    private readonly Stack<Node3D> _ringPool = new();

    public override void _Ready()
    {
        _capPlayer = _capEnemy = InitialCapacity;
        _playerMm = MakeUnitMultiMesh(new Color(0.36f, 0.62f, 0.94f));
        _enemyMm = MakeUnitMultiMesh(new Color(0.92f, 0.32f, 0.28f));
        _playerMmi = new MultiMeshInstance3D { Multimesh = _playerMm };
        _enemyMmi = new MultiMeshInstance3D { Multimesh = _enemyMm };
        AddChild(_playerMmi);
        AddChild(_enemyMmi);
    }

    private static MultiMesh MakeUnitMultiMesh(Color color)
    {
        var mm = new MultiMesh
        {
            TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = new CapsuleMesh { Radius = 0.4f, Height = 1.2f },
            InstanceCount = InitialCapacity,
            VisibleInstanceCount = 0,
        };
        var mat = new StandardMaterial3D { AlbedoColor = color, Roughness = 0.7f };
        mm.Mesh.SurfaceSetMaterial(0, mat);
        return mm;
    }

    public void Sync(GameSim sim, float alpha)
    {
        // Units — split by team into two GPU batches
        int nPlayer = 0, nEnemy = 0;
        foreach (var u in sim.Units) if (u.Team == Team.Player) nPlayer++; else nEnemy++;
        EnsureCapacity(ref _playerMm, ref _capPlayer, nPlayer);
        EnsureCapacity(ref _enemyMm, ref _capEnemy, nEnemy);

        int iP = 0, iE = 0;
        foreach (SimUnit unit in sim.Units)
        {
            Vector3 pos = unit.PrevPos.Lerp(unit.Pos, alpha) + new Vector3(0, 0.6f, 0);
            var xf = new Transform3D(Basis.Identity, pos);
            if (unit.Team == Team.Player) _playerMm.SetInstanceTransform(iP++, xf);
            else _enemyMm.SetInstanceTransform(iE++, xf);
        }
        _playerMm.VisibleInstanceCount = iP;
        _enemyMm.VisibleInstanceCount = iE;

        foreach (SimBuilding b in sim.Buildings)
        {
            if (_buildingViews.ContainsKey(b.Id)) continue;
            var node = CreateBuildingVisual(b);
            _buildingViews[b.Id] = node;
            AddChild(node);
        }

        foreach (SimResource r in sim.Resources)
        {
            if (_resourceViews.ContainsKey(r.Id)) continue;
            var node = CreateResourceVisual(r);
            _resourceViews[r.Id] = node;
            AddChild(node);
        }
        // Remove depleted resources
        var deadRes = new List<int>();
        foreach (var kv in _resourceViews) if (!sim.ResourceById.ContainsKey(kv.Key)) deadRes.Add(kv.Key);
        foreach (int id in deadRes) { _resourceViews[id].QueueFree(); _resourceViews.Remove(id); }

        foreach (var kv in _activeRings)
        {
            if (!sim.UnitById.TryGetValue(kv.Key, out var unit)) continue;
            kv.Value.Position = unit.PrevPos.Lerp(unit.Pos, alpha);
        }
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
