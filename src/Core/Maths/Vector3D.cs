/* Ported from source/maths/Vector3D.h/.cpp — float 3D vector (render/Godot interop). */

using System;

namespace Rts0.Core;

/// <summary>
/// Float 3D vector for render/Godot interop. Distinct from <see cref="FixedVector3D"/>.
/// Direct port of 0 A.D. <c>CVector3D</c> (source/maths/Vector3D.h/.cpp).
/// </summary>
public readonly struct Vector3D : IEquatable<Vector3D>
{
    public readonly float X;
    public readonly float Y;
    public readonly float Z;

    public Vector3D(float x, float y, float z) { X = x; Y = y; Z = z; }

    public static Vector3D Zero => new(0f, 0f, 0f);
    public static Vector3D MaxValue => new(float.MaxValue, float.MaxValue, float.MaxValue);
    public static Vector3D MinValue => new(-float.MaxValue, -float.MaxValue, -float.MaxValue);

    public float this[int index] => index == 0 ? X : index == 1 ? Y : Z;

    public static Vector3D operator +(Vector3D a, Vector3D b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3D operator -(Vector3D a, Vector3D b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3D operator -(Vector3D v) => new(-v.X, -v.Y, -v.Z);
    public static Vector3D operator *(Vector3D v, float f) => new(v.X * f, v.Y * f, v.Z * f);
    public static Vector3D operator /(Vector3D v, float f) => new(v.X / f, v.Y / f, v.Z / f);

    public float Dot(Vector3D v) => X * v.X + Y * v.Y + Z * v.Z;

    public Vector3D Cross(Vector3D v) => new(
        Y * v.Z - Z * v.Y,
        Z * v.X - X * v.Z,
        X * v.Y - Y * v.X);

    public float LengthSquared() => Dot(this);
    public float Length() => MathF.Sqrt(LengthSquared());

    public Vector3D Normalized()
    {
        float m = Length();
        return m > 1e-8f ? new Vector3D(X / m, Y / m, Z / m) : this;
    }

    public bool IsZero() => X == 0f && Y == 0f && Z == 0f;

    public static float MaxComponent(Vector3D v) => Math.Max(v.X, Math.Max(v.Y, v.Z));

    /// <summary>Convert simulation FixedVector3D to float.</summary>
    public static Vector3D FromFixed(FixedVector3D v) => new(v.X.ToFloat(), v.Y.ToFloat(), v.Z.ToFloat());
    public FixedVector3D ToFixed() => new(Fixed.FromFloat(X), Fixed.FromFloat(Y), Fixed.FromFloat(Z));

    public float[] ToArray() => new[] { X, Y, Z };

    public bool Equals(Vector3D other) => X == other.X && Y == other.Y && Z == other.Z;
    public override bool Equals(object obj) => obj is Vector3D v && Equals(v);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public static bool operator ==(Vector3D a, Vector3D b) => a.Equals(b);
    public static bool operator !=(Vector3D a, Vector3D b) => !a.Equals(b);
    public override string ToString() => $"({X:F3}, {Y:F3}, {Z:F3})";
}
