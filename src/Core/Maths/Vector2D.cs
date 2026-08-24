/* Ported from source/maths/Vector2D.h/.cpp — float 2D vector (render/Godot interop). */

using System;

namespace Rts0.Core;

/// <summary>
/// Float 2D vector for render/Godot interop. Distinct from deterministic <see cref="FixedVector2D"/>.
/// Direct port of 0 A.D. <c>CVector2D</c> (source/maths/Vector2D.h/.cpp).
/// </summary>
public readonly struct Vector2D : IEquatable<Vector2D>
{
    public readonly float X;
    public readonly float Y;

    public Vector2D(float x, float y) { X = x; Y = y; }

    public static Vector2D Zero => new(0f, 0f);

    public static Vector2D operator +(Vector2D a, Vector2D b) => new(a.X + b.X, a.Y + b.Y);
    public static Vector2D operator -(Vector2D a, Vector2D b) => new(a.X - b.X, a.Y - b.Y);
    public static Vector2D operator -(Vector2D v) => new(-v.X, -v.Y);
    public static Vector2D operator *(Vector2D v, float f) => new(v.X * f, v.Y * f);
    public static Vector2D operator /(Vector2D v, float f) => new(v.X / f, v.Y / f);

    public float Dot(Vector2D a) => X * a.X + Y * a.Y;
    public float LengthSquared() => Dot(this);
    public float Length() => MathF.Sqrt(LengthSquared());

    public Vector2D Normalized()
    {
        float m = Length();
        return m > 1e-8f ? new Vector2D(X / m, Y / m) : this;
    }

    /// <summary>Rotated counter-clockwise by angle radians. See CVector2D::Rotated.</summary>
    public Vector2D Rotated(float angle)
    {
        float c = MathF.Cos(angle);
        float s = MathF.Sin(angle);
        return new Vector2D(c * X - s * Y, s * X + c * Y);
    }

    public float[] ToArray() => new[] { X, Y };

    /// <summary>Convert simulation FixedVector2D to float render vector.</summary>
    public static Vector2D FromFixed(FixedVector2D v) => new(v.X.ToFloat(), v.Y.ToFloat());
    public FixedVector2D ToFixed() => new(Fixed.FromFloat(X), Fixed.FromFloat(Y));

    public bool Equals(Vector2D other) => X == other.X && Y == other.Y;
    public override bool Equals(object obj) => obj is Vector2D v && Equals(v);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public static bool operator ==(Vector2D a, Vector2D b) => a.Equals(b);
    public static bool operator !=(Vector2D a, Vector2D b) => !a.Equals(b);
    public override string ToString() => $"({X:F3}, {Y:F3})";
}
