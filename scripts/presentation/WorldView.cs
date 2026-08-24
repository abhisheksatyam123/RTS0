using System.Collections.Generic;
using Godot;
using Rts0.Core;

namespace Rts0.Presentation;

// Presentation layer: reads sim state, never mutates it.
// All units render through ONE MultiMeshInstance3D -> single draw call on the GPU,
// independent of unit count (vs one Node3D + transform sync per unit).
public partial class WorldView : Node3D
{
    private const int InitialCapacity = 256;

    private MultiMeshInstance3D _unitsMultiMeshInstance;
    private MultiMesh _unitsMultiMesh;
    private int _capacity;

    private readonly Dictionary<int, Node3D> _buildingViews = new();
    private readonly Dictionary<int, Node3D> _activeRings = new();
    private readonly Stack<Node3D> _ringPool = new();

    public override void _Ready()
    {
        _capacity = InitialCapacity;
        _unitsMultiMesh = new MultiMesh
        {
            TransformFormat = MultiMesh.TransformFormatEnum.Transform3D,
            Mesh = new CapsuleMesh { Radius = 0.4f, Height = 1.2f },
            InstanceCount = _capacity,
            VisibleInstanceCount = 0,
        };
        var material = new StandardMaterial3D
        {
            AlbedoColor = new Color(0.36f, 0.62f, 0.94f),
            Roughness = 0.7f,
        };
        _unitsMultiMesh.Mesh.SurfaceSetMaterial(0, material);
        _unitsMultiMeshInstance = new MultiMeshInstance3D { Multimesh = _unitsMultiMesh };
        AddChild(_unitsMultiMeshInstance);
    }

    public void Sync(GameSim sim, float alpha)
    {
        if (sim.Units.Count > _capacity)
        {
            // Growing InstanceCount resets the buffer; we refill it right after anyway.
            _capacity = sim.Units.Count * 2;
            _unitsMultiMesh.InstanceCount = _capacity;
        }

        int index = 0;
        foreach (SimUnit unit in sim.Units)
        {
            Vector3 pos = unit.PrevPos.Lerp(unit.Pos, alpha);
            _unitsMultiMesh.SetInstanceTransform(index++,
                new Transform3D(Basis.Identity, pos + new Vector3(0.0f, 0.6f, 0.0f)));
        }
        _unitsMultiMesh.VisibleInstanceCount = index;

        foreach (SimBuilding b in sim.Buildings)
        {
            if (_buildingViews.ContainsKey(b.Id)) continue;
            Node3D node = CreateBuildingVisual(b);
            _buildingViews[b.Id] = node;
            AddChild(node);
        }

        // Keep selection rings glued to their units.
        foreach (KeyValuePair<int, Node3D> kvp in _activeRings)
        {
            if (!sim.UnitById.TryGetValue(kvp.Key, out SimUnit unit)) continue;
            Vector3 pos = unit.PrevPos.Lerp(unit.Pos, alpha);
            kvp.Value.Position = pos;
        }
    }

    public void SetSelected(int unitId, bool selected)
    {
        if (selected)
        {
            if (_activeRings.ContainsKey(unitId)) return;
            Node3D ring = _ringPool.Count > 0 ? _ringPool.Pop() : CreateRing();
            ring.Visible = true;
            AddChild(ring);
            _activeRings[unitId] = ring;
        }
        else
        {
            if (!_activeRings.Remove(unitId, out Node3D ring)) return;
            ring.Visible = false;
            RemoveChild(ring);
            _ringPool.Push(ring);
        }
    }

    private static Node3D CreateRing()
    {
        var material = new StandardMaterial3D
        {
            AlbedoColor = new Color(0.3f, 0.9f, 1.0f),
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
        };
        return new MeshInstance3D
        {
            Name = "Ring",
            Mesh = new TorusMesh { InnerRadius = 0.42f, OuterRadius = 0.58f, Material = material },
            Position = new Vector3(0.0f, 0.06f, 0.0f),
        };
    }

    private static Node3D CreateBuildingVisual(SimBuilding b)
    {
        float size = b.SizeMeters;
        Vector3 centerOffset = new(size * 0.5f, size * 0.5f, size * 0.5f);

        var root = new Node3D { Position = b.BasePos };

        var bodyMat = new StandardMaterial3D
        {
            AlbedoColor = b.IsCenter ? new Color(0.85f, 0.74f, 0.55f) : new Color(0.72f, 0.64f, 0.52f),
            Roughness = 0.9f,
        };
        root.AddChild(new MeshInstance3D
        {
            Mesh = new BoxMesh { Size = new Vector3(size, size, size), Material = bodyMat },
            Position = centerOffset,
        });

        if (b.IsCenter)
        {
            var roofMat = new StandardMaterial3D { AlbedoColor = new Color(0.55f, 0.32f, 0.26f) };
            root.AddChild(new MeshInstance3D
            {
                Mesh = new PrismMesh
                {
                    Size = new Vector3(size * 1.15f, size * 0.5f, size * 1.15f),
                    Material = roofMat,
                },
                Position = centerOffset + new Vector3(0.0f, size * 0.75f, 0.0f),
            });
        }
        return root;
    }
}
