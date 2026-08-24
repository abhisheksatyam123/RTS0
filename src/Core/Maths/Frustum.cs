/// Ported from source/maths/Frustum.h/.cpp — 0 A.D. GPL-2.0 — frustum culling.
using System;

namespace Rts0.Core;

/// <summary>Frustum as collection of planes. Port of 0 A.D. <c>CFrustum</c> (source/maths/Frustum.h/.cpp).</summary>
public sealed class Frustum
{
    public const int MaxPlanes = 10;
    private readonly Plane[] _planes = new Plane[MaxPlanes];
    private int _numPlanes;

    public Frustum() { for (int i = 0; i < MaxPlanes; i++) _planes[i] = new Plane(); }

    public int NumPlanes => _numPlanes;
    public void SetNumPlanes(int n) { _numPlanes = n >= MaxPlanes ? MaxPlanes - 1 : n; }
    public void AddPlane(Plane p) { if (_numPlanes < MaxPlanes) _planes[_numPlanes++] = p; }
    public Plane this[int i] { get => _planes[i]; set => _planes[i] = value; }

    /// <summary>Transform planes by matrix. See Frustum.cpp Transform.</summary>
    public void Transform(Matrix3D m)
    {
        for (int i = 0; i < _numPlanes; i++)
        {
            var n = _planes[i].Norm;
            // rotate normal (ignore translation): m * n
            var rn = new Vector3D(m._11*n.X+m._12*n.Y+m._13*n.Z, m._21*n.X+m._22*n.Y+m._23*n.Z, m._31*n.X+m._32*n.Y+m._33*n.Z);
            var pt = m.Transform(n * -_planes[i].Dist);
            _planes[i].Set(rn, pt);
            _planes[i].Normalize();
        }
    }

    public bool IsPointVisible(Vector3D p)
    {
        for (int i = 0; i < _numPlanes; i++)
            if (_planes[i].IsPointOnBackSide(p)) return false;
        return true;
    }

    public bool DoesSegmentIntersect(Vector3D start, Vector3D end)
    {
        if (IsPointVisible(start) || IsPointVisible(end)) return true;
        for (int i = 0; i < _numPlanes; i++)
            if (_planes[i].FindLineSegIntersection(start, end, out var hit) && IsPointVisible(hit)) return true;
        return false;
    }

    public bool IsSphereVisible(Vector3D center, float radius)
    {
        for (int i = 0; i < _numPlanes; i++)
            if (-_planes[i].DistanceToPlane(center) > radius) return false;
        return true;
    }

    public bool IsBoxVisible(Vector3D pos, BoundingBoxAligned b)
    {
        var mn = pos + b.Min; var mx = pos + b.Max;
        for (int i = 0; i < _numPlanes; i++)
        {
            var n = _planes[i].Norm;
            var far = new Vector3D(n.X > 0 ? mx.X : mn.X, n.Y > 0 ? mx.Y : mn.Y, n.Z > 0 ? mx.Z : mn.Z);
            if (_planes[i].IsPointOnBackSide(far)) return false;
        }
        return true;
    }

    public bool IsBoxVisible(BoundingBoxAligned b)
    {
        for (int i = 0; i < _numPlanes; i++)
        {
            var n = _planes[i].Norm;
            var far = new Vector3D(n.X > 0 ? b.Max.X : b.Min.X, n.Y > 0 ? b.Max.Y : b.Min.Y, n.Z > 0 ? b.Max.Z : b.Min.Z);
            if (_planes[i].IsPointOnBackSide(far)) return false;
        }
        return true;
    }
    // Aliases matching task description
    public bool ContainsPoint(Vector3D p) => IsPointVisible(p);
    public bool ContainsSphere(Vector3D c, float r) => IsSphereVisible(c, r);
    public bool ContainsBox(BoundingBoxAligned b) => IsBoxVisible(b);
}
