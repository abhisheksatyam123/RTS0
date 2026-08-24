/* Ported from source/maths/BoundingBoxAligned.h/.cpp, BoundingBoxOriented.h/.cpp, BoundingSphere.h/.cpp
 * Float-side bounds for render/culling. Distinct from deterministic simulation geometry. */

using System;

namespace Rts0.Core;

/// <summary>
/// Axis-aligned bounding box. Port of 0 A.D. <c>CBoundingBoxAligned</c> (source/maths/BoundingBoxAligned.h/.cpp).
/// Float render-side; use Fixed types for simulation.
/// </summary>
public struct BoundingBoxAligned : IEquatable<BoundingBoxAligned>
{
    public Vector3D Min;
    public Vector3D Max;

    public static BoundingBoxAligned Empty => new() { Min = Vector3D.MaxValue, Max = Vector3D.MinValue };

    public BoundingBoxAligned(Vector3D min, Vector3D max) { Min = min; Max = max; }

    public bool IsEmpty() => Min == Vector3D.MaxValue && Max == Vector3D.MinValue;
    public void SetEmpty() { Min = Vector3D.MaxValue; Max = Vector3D.MinValue; }

    public void Extend(Vector3D min, Vector3D max)
    {
        if (min.X < Min.X) Min = new Vector3D(min.X, Min.Y, Min.Z);
        if (min.Y < Min.Y) Min = new Vector3D(Min.X, min.Y, Min.Z);
        if (min.Z < Min.Z) Min = new Vector3D(Min.X, Min.Y, min.Z);
        if (max.X > Max.X) Max = new Vector3D(max.X, Max.Y, Max.Z);
        if (max.Y > Max.Y) Max = new Vector3D(Max.X, max.Y, Max.Z);
        if (max.Z > Max.Z) Max = new Vector3D(Max.X, Max.Y, max.Z);
    }

    public void Expand(float amount)
    {
        var d = new Vector3D(amount, amount, amount);
        Min -= d; Max += d;
    }

    public Vector3D Center => (Min + Max) * 0.5f;
    public float Volume
    {
        get
        {
            var v = Max - Min;
            return Math.Max(v.X, 0f) * Math.Max(v.Y, 0f) * Math.Max(v.Z, 0f);
        }
    }

    public bool Contains(Vector3D p) => p.X >= Min.X && p.X <= Max.X && p.Y >= Min.Y && p.Y <= Max.Y && p.Z >= Min.Z && p.Z <= Max.Z;
    public bool Contains(BoundingBoxAligned b) => Contains(b.Min) && Contains(b.Max);
    public bool Intersects(BoundingBoxAligned b) => Min.X <= b.Max.X && Max.X >= b.Min.X && Min.Y <= b.Max.Y && Max.Y >= b.Min.Y && Min.Z <= b.Max.Z && Max.Z >= b.Min.Z;
    public bool Intersects(BoundingSphere s) => s.Intersects(this);

    /// <summary>Ray-AABB. See BoundingBoxAligned.cpp RayIntersect.</summary>
    public bool RayIntersect(Vector3D origin, Vector3D dir, out float tMin, out float tMax)
    {
        tMin = float.NegativeInfinity; tMax = float.PositiveInfinity;
        for (int i = 0; i < 3; i++)
        {
            float o = origin[i], d = dir[i], mn = Min[i], mx = Max[i];
            if (Math.Abs(d) < 1e-8f)
            {
                if (o < mn || o > mx) { tMin = 0; tMax = 0; return false; }
            }
            else
            {
                float t1 = (mn - o) / d, t2 = (mx - o) / d;
                if (t1 > t2) (t1, t2) = (t2, t1);
                if (t1 > tMin) tMin = t1;
                if (t2 < tMax) tMax = t2;
                if (tMin > tMax || tMax < 0) return false;
            }
        }
        return true;
    }

    public bool Equals(BoundingBoxAligned other) => Min == other.Min && Max == other.Max;
    public override bool Equals(object obj) => obj is BoundingBoxAligned b && Equals(b);
    public override int GetHashCode() => HashCode.Combine(Min, Max);
}

/// <summary>
/// Oriented bounding box. Port of 0 A.D. <c>CBoundingBoxOriented</c> (source/maths/BoundingBoxOriented.h/.cpp).
/// </summary>
public struct BoundingBoxOriented
{
    public Vector3D Center;
    public Vector3D HalfSize;
    public Vector3D U, V, W; // orthonormal basis

    public static BoundingBoxOriented Empty => new();

    public bool IsEmpty() => Center.IsZero() && U.IsZero() && V.IsZero() && W.IsZero() && HalfSize.IsZero();

    public void GetCorner(int u, int v, int w, out Vector3D c) => c = Center + U * (u * HalfSize.X) + V * (v * HalfSize.Y) + W * (w * HalfSize.Z);

    public bool Contains(Vector3D p)
    {
        Vector3D d = p - Center;
        return Math.Abs(d.Dot(U)) <= HalfSize.X + 1e-6f && Math.Abs(d.Dot(V)) <= HalfSize.Y + 1e-6f && Math.Abs(d.Dot(W)) <= HalfSize.Z + 1e-6f;
    }

    /// <summary>Ray-OBB slab test. See BoundingBoxOriented.cpp RayIntersect.</summary>
    public bool RayIntersect(Vector3D origin, Vector3D dir, out float tMin, out float tMax)
    {
        tMin = float.NegativeInfinity; tMax = float.PositiveInfinity;
        Vector3D p = Center - origin;
        Vector3D[] basis = { U, V, W };
        for (int i = 0; i < 3; i++)
        {
            float e = basis[i].Dot(p), f = basis[i].Dot(dir), hs = HalfSize[i];
            if (Math.Abs(f) > 1e-10f)
            {
                float t1 = (e + hs) / f, t2 = (e - hs) / f;
                if (t1 > t2) (t1, t2) = (t2, t1);
                if (t1 > tMin) tMin = t1;
                if (t2 < tMax) tMax = t2;
                if (tMin > tMax || tMax < 0) return false;
            }
            else if (e > hs || -e > hs) return false;
        }
        return true;
    }
}

/// <summary>
/// Bounding sphere. Port of 0 A.D. <c>CBoundingSphere</c> (source/maths/BoundingSphere.h/.cpp).
/// </summary>
public readonly struct BoundingSphere : IEquatable<BoundingSphere>
{
    public readonly Vector3D Center;
    public readonly float Radius;

    public BoundingSphere(Vector3D center, float radius) { Center = center; Radius = radius; }

    /// <summary>From swept AABB. See BoundingSphere.cpp FromSweptBox.</summary>
    public static BoundingSphere FromSweptBox(BoundingBoxAligned b)
    {
        float mx = Math.Max(Math.Abs(b.Min.X), Math.Abs(b.Max.X));
        float my = Math.Max(Math.Abs(b.Min.Y), Math.Abs(b.Max.Y));
        float mz = Math.Max(Math.Abs(b.Min.Z), Math.Abs(b.Max.Z));
        return new BoundingSphere(Vector3D.Zero, MathF.Sqrt(mx * mx + my * my + mz * mz));
    }

    public bool Contains(Vector3D p) => (p - Center).LengthSquared() <= Radius * Radius;
    public bool Intersects(BoundingSphere s) => (s.Center - Center).LengthSquared() <= (Radius + s.Radius) * (Radius + s.Radius);

    public bool Intersects(BoundingBoxAligned b)
    {
        float cx = MathUtil.Clamp(Center.X, b.Min.X, b.Max.X);
        float cy = MathUtil.Clamp(Center.Y, b.Min.Y, b.Max.Y);
        float cz = MathUtil.Clamp(Center.Z, b.Min.Z, b.Max.Z);
        var closest = new Vector3D(cx, cy, cz);
        return (closest - Center).LengthSquared() <= Radius * Radius;
    }

    /// <summary>Ray-sphere. See BoundingSphere.cpp RayIntersect.</summary>
    public bool RayIntersect(Vector3D origin, Vector3D dir)
    {
        Vector3D v = Center - origin;
        float pc = dir.Dot(v);
        if (pc > 0) v = dir * pc - v;
        return v.LengthSquared() <= Radius * Radius;
    }

    public bool Equals(BoundingSphere other) => Center == other.Center && Radius == other.Radius;
    public override bool Equals(object obj) => obj is BoundingSphere s && Equals(s);
    public override int GetHashCode() => HashCode.Combine(Center, Radius);
}
