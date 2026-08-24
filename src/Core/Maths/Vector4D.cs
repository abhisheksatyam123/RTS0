/* Ported from source/maths/Vector4D.h — float 4D vector (render/Godot interop). */

using System;

namespace Rts0.Core;

/// <summary>
/// Float 4D vector for render/Godot interop.
/// Direct port of 0 A.D. <c>CVector4D</c> (source/maths/Vector4D.h).
/// </summary>
public readonly struct Vector4D : IEquatable<Vector4D>
{
    public readonly float X;
    public readonly float Y;
    public readonly float Z;
    public readonly float W;

    public Vector4D(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }

    public static Vector4D operator +(Vector4D a, Vector4D b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W);
    public static Vector4D operator -(Vector4D a, Vector4D b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z, a.W - b.W);
    public static Vector4D operator -(Vector4D v) => new(-v.X, -v.Y, -v.Z, -v.W);
    public static Vector4D operator *(Vector4D a, Vector4D b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z, a.W * b.W);
    public static Vector4D operator *(Vector4D v, float f) => new(v.X * f, v.Y * f, v.Z * f, v.W * f);
    public static Vector4D operator /(Vector4D v, float f) => new(v.X / f, v.Y / f, v.Z / f, v.W / f);

    public float Dot(Vector4D a) => X * a.X + Y * a.Y + Z * a.Z + W * a.W;

    public float LengthSquared() => Dot(this);
    public float Length() => MathF.Sqrt(LengthSquared());

    public Vector4D Normalized()
    {
        float m = Length();
        return m > 1e-8f ? new Vector4D(X / m, Y / m, Z / m, W / m) : this;
    }

    public float[] ToArray() => new[] { X, Y, Z, W };

    public bool Equals(Vector4D other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    public override bool Equals(object obj) => obj is Vector4D v && Equals(v);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public static bool operator ==(Vector4D a, Vector4D b) => a.Equals(b);
    public static bool operator !=(Vector4D a, Vector4D b) => !a.Equals(b);
    public override string ToString() => $"({X:F3}, {Y:F3}, {Z:F3}, {W:F3})";
}
