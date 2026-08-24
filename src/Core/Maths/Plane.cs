/// Ported from source/maths/Plane.h/.cpp — 0 A.D. GPL-2.0 — plane Ax+By+Cz+D=0.
using System;

namespace Rts0.Core;

/// <summary>Plane <c>Ax+By+Cz+D=0</c>. Port of 0 A.D. <c>CPlane</c> (source/maths/Plane.h/.cpp).</summary>
public sealed class Plane
{
    public Vector3D Norm;
    public float Dist;
    public const float Eps = 0.001f;

    public Plane() { Norm = Vector3D.Zero; Dist = 0f; }
    public Plane(Vector3D norm, float dist) { Norm = norm; Dist = dist; }
    public Plane(Vector4D coeffs) { Norm = new Vector3D(coeffs.X, coeffs.Y, coeffs.Z); Dist = coeffs.W; }

    /// <summary>Set from 3 points. See Plane.cpp Set(p1,p2,p3).</summary>
    public void Set(Vector3D p1, Vector3D p2, Vector3D p3)
    {
        var d1 = p2 - p1; var d2 = p3 - p1;
        var n = d2.Cross(d1);
        Set(n, p1);
    }

    /// <summary>Set from normal and point. See Plane.cpp Set(norm,point).</summary>
    public void Set(Vector3D norm, Vector3D point)
    {
        Norm = norm;
        Dist = -(norm.X * point.X + norm.Y * point.Y + norm.Z * point.Z);
    }

    /// <summary>Normalize plane equation. See Plane.cpp Normalize().</summary>
    public void Normalize()
    {
        float scale = 1f / Norm.Length();
        Norm = new Vector3D(Norm.X * scale, Norm.Y * scale, Norm.Z * scale);
        Dist *= scale;
    }

    /// <summary>Signed distance to plane. See Plane.h DistanceToPlane.</summary>
    public float DistanceToPlane(Vector3D p) => Norm.X * p.X + Norm.Y * p.Y + Norm.Z * p.Z + Dist;

    /// <summary>Classify point. See Plane.cpp ClassifyPoint.</summary>
    public PlaneSide ClassifyPoint(Vector3D p)
    {
        float d = DistanceToPlane(p);
        if (d > Eps) return PlaneSide.Front;
        if (d < -Eps) return PlaneSide.Back;
        return PlaneSide.On;
    }

    /// <summary>True if point on back side. See Plane.h IsPointOnBackSide.</summary>
    public bool IsPointOnBackSide(Vector3D p) => DistanceToPlane(p) < -Eps;

    /// <summary>Intersect line segment. See Plane.cpp FindLineSegIntersection.</summary>
    public bool FindLineSegIntersection(Vector3D start, Vector3D end, out Vector3D hit)
    {
        float d1 = DistanceToPlane(start), d2 = DistanceToPlane(end);
        if ((d1 < 0 && d2 < 0) || (d1 >= 0 && d2 >= 0)) { hit = Vector3D.Zero; return false; }
        float t = -d1 / (d2 - d1);
        hit = new Vector3D(MathUtil.Lerp(start.X, end.X, t), MathUtil.Lerp(start.Y, end.Y, t), MathUtil.Lerp(start.Z, end.Z, t));
        return true;
    }

    /// <summary>Intersect ray. See Plane.cpp FindRayIntersection.</summary>
    public bool FindRayIntersection(Vector3D start, Vector3D dir, out Vector3D hit)
    {
        float dot = Norm.Dot(dir);
        if (dot == 0f) { hit = Vector3D.Zero; return false; }
        hit = start - dir * (DistanceToPlane(start) / dot);
        return true;
    }
}

/// <summary>Plane side. Port of 0 A.D. <c>PLANESIDE</c> (maths/Plane.h).</summary>
public enum PlaneSide { Front, Back, On }
