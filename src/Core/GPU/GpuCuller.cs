// Ported from 0ad Frustum culling — GPU-aware culler using Rts0.Core.Frustum
// Mirrors source/graphics/ Frustum culling + Thronefall GPU culling.
using System.Collections.Generic;
using Godot;
using Rts0.Core;

namespace Rts0.Core.Gpu;

public sealed class GpuCuller
{
    private Frustum _frustum = new();
    private readonly List<int> _visible = new();

    public IReadOnlyList<int> Visible => _visible;

    public void UpdateFrustum(Camera3D cam)
    {
        _frustum = new Rts0.Core.Frustum();
        var planes = cam.GetFrustum();
        int n = planes.Count;
        _frustum.SetNumPlanes(n);
        for (int i = 0; i < n && i < Rts0.Core.Frustum.MaxPlanes; i++)
        {
            Godot.Plane gp = planes[i];
            var nrm = new Vector3D(gp.Normal.X, gp.Normal.Y, gp.Normal.Z);
            var pt = nrm * gp.D;
            var p = new Rts0.Core.Plane();
            p.Set(nrm, pt);
            p.Normalize();
            _frustum[i] = p;
        }
    }

    public void CullUnits(IReadOnlyList<SimUnit> units, float radius = 0.6f)
    {
        _visible.Clear();
        foreach (var u in units)
        {
            var pos = new Vector3D(u.Pos.X, u.Pos.Y, u.Pos.Z);
            if (_frustum.IsSphereVisible(pos, radius)) _visible.Add(u.Id);
        }
    }

    public void CullBuildings(IReadOnlyList<SimBuilding> buildings)
    {
        _visible.Clear();
        foreach (var b in buildings)
        {
            var pos = new Vector3D(b.BasePos.X + b.SizeMeters * 0.5f, 0, b.BasePos.Z + b.SizeMeters * 0.5f);
            var box = new BoundingBoxAligned(new Vector3D(-b.SizeMeters * 0.5f, 0, -b.SizeMeters * 0.5f), new Vector3D(b.SizeMeters * 0.5f, b.SizeMeters, b.SizeMeters * 0.5f));
            if (_frustum.IsBoxVisible(pos, box)) _visible.Add(b.Id);
        }
    }

    public bool IsVisible(Vector3D center, float radius) => _frustum.IsSphereVisible(center, radius);
}
