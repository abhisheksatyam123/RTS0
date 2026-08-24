/* Aggregates source/maths/Vector2D.h, Vector3D.h, Vector4D.h — float maths for Godot interop. */

using System;

namespace Rts0.Core;

/// <summary>
/// Helpers bridging float render maths and Godot/0 A.D. conventions.
/// Complements <see cref="Vector2D"/>, <see cref="Vector3D"/>, <see cref="Vector4D"/>.
/// Ported from source/maths/Vector2D.h, Vector3D.h, Vector4D.h and MathUtil.h SQR.
/// </summary>
public static class VectorUtil
{
    /// <summary>Max component of Vector3D. See Vector3D.cpp MaxComponent.</summary>
    public static float MaxComponent(Vector3D v) => Vector3D.MaxComponent(v);

    /// <summary>Clamp float vector length.</summary>
    public static Vector2D ClampLength(Vector2D v, float maxLen)
    {
        float lsq = v.LengthSquared();
        if (lsq <= maxLen * maxLen) return v;
        return v.Normalized() * maxLen;
    }

    public static Vector3D ClampLength(Vector3D v, float maxLen)
    {
        float lsq = v.LengthSquared();
        if (lsq <= maxLen * maxLen) return v;
        return v.Normalized() * maxLen;
    }

    /// <summary>Lerp between float vectors.</summary>
    public static Vector2D Lerp(Vector2D a, Vector2D b, float t) => a + (b - a) * t;
    public static Vector3D Lerp(Vector3D a, Vector3D b, float t) => a + (b - a) * t;
    public static Vector4D Lerp(Vector4D a, Vector4D b, float t) => a + (b - a) * t;

    /// <summary>Distance helpers.</summary>
    public static float Distance(Vector2D a, Vector2D b) => (b - a).Length();
    public static float Distance(Vector3D a, Vector3D b) => (b - a).Length();
    public static float DistanceSquared(Vector3D a, Vector3D b) => (b - a).LengthSquared();
}
