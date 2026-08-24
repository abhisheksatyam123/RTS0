/* Ported from source/maths/FixedVector3D.h — deterministic 3D vector (CFixedVector3D). */

using System;

namespace Rts0.Core;

/// <summary>
/// Deterministic 3D vector using <see cref="Fixed"/> (15.16).
/// Direct port of 0 A.D. <c>CFixedVector3D</c> (source/maths/FixedVector3D.h).
/// All ops are deterministic; no float involved.
/// </summary>
public readonly struct FixedVector3D : IEquatable<FixedVector3D>
{
    public readonly Fixed X;
    public readonly Fixed Y;
    public readonly Fixed Z;

    public FixedVector3D(Fixed x, Fixed y, Fixed z) { X = x; Y = y; Z = z; }

    public static FixedVector3D operator +(FixedVector3D a, FixedVector3D b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static FixedVector3D operator -(FixedVector3D a, FixedVector3D b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static FixedVector3D operator -(FixedVector3D v) => new(-v.X, -v.Y, -v.Z);
    public static FixedVector3D operator *(FixedVector3D v, int n) => new(v.X * n, v.Y * n, v.Z * n);
    public static FixedVector3D operator /(FixedVector3D v, int n) => new(v.X / n, v.Y / n, v.Z / n);

    /// <summary>Scale by fixed factor (deterministic).</summary>
    public FixedVector3D Multiply(Fixed n) => new(X.Multiply(n), Y.Multiply(n), Z.Multiply(n));

    public bool IsZero() => X.IsZero() && Y.IsZero() && Z.IsZero();

    /// <summary>Length via isqrt64 on 64-bit squared magnitude. See FixedVector3D.h Length().</summary>
    public Fixed Length()
    {
        ulong xx = (ulong)(long)X.RawValue * (ulong)(long)X.RawValue;
        ulong yy = (ulong)(long)Y.RawValue * (ulong)(long)Y.RawValue;
        ulong zz = (ulong)(long)Z.RawValue * (ulong)(long)Z.RawValue;
        ulong d2 = xx + yy + zz;
        uint d = SqrtUtil.Isqrt64(d2);
        return Fixed.FromRaw((int)d);
    }

    public int CompareLength(Fixed cmp)
    {
        ulong d2 = (ulong)(long)X.RawValue * (ulong)(long)X.RawValue
                 + (ulong)(long)Y.RawValue * (ulong)(long)Y.RawValue
                 + (ulong)(long)Z.RawValue * (ulong)(long)Z.RawValue;
        ulong cmp2 = (ulong)(long)cmp.RawValue * (ulong)(long)cmp.RawValue;
        if (d2 < cmp2) return -1;
        if (d2 > cmp2) return 1;
        return 0;
    }

    /// <summary>Normalize to length ~1. No-op if zero.</summary>
    public FixedVector3D Normalized()
    {
        if (IsZero()) return this;
        Fixed len = Length();
        return new FixedVector3D(X / len, Y / len, Z / len);
    }

    /// <summary>Normalize to target length n. No-op if zero.</summary>
    public FixedVector3D Normalized(Fixed n)
    {
        Fixed len = Length();
        if (len.IsZero()) return this;
        return new FixedVector3D(X.MulDiv(n, len), Y.MulDiv(n, len), Z.MulDiv(n, len));
    }

    /// <summary>Dot product. See FixedVector3D.h Dot().</summary>
    public Fixed Dot(FixedVector3D v)
    {
        long x = (long)X.RawValue * v.X.RawValue;
        long y = (long)Y.RawValue * v.Y.RawValue;
        long z = (long)Z.RawValue * v.Z.RawValue;
        long sum = x + y + z;
        sum >>= Fixed.FractBits;
        return Fixed.FromRaw((int)sum);
    }

    /// <summary>Cross product. See FixedVector3D.h Cross().</summary>
    public FixedVector3D Cross(FixedVector3D v)
    {
        long yvz = (long)Y.RawValue * v.Z.RawValue;
        long zvy = (long)Z.RawValue * v.Y.RawValue;
        long x = (yvz - zvy) >> Fixed.FractBits;

        long zvx = (long)Z.RawValue * v.X.RawValue;
        long xvz = (long)X.RawValue * v.Z.RawValue;
        long y = (zvx - xvz) >> Fixed.FractBits;

        long xvy = (long)X.RawValue * v.Y.RawValue;
        long yvx = (long)Y.RawValue * v.X.RawValue;
        long z = (xvy - yvx) >> Fixed.FractBits;

        return new FixedVector3D(Fixed.FromRaw((int)x), Fixed.FromRaw((int)y), Fixed.FromRaw((int)z));
    }

    public bool Equals(FixedVector3D other) => X == other.X && Y == other.Y && Z == other.Z;
    public override bool Equals(object obj) => obj is FixedVector3D v && Equals(v);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public static bool operator ==(FixedVector3D a, FixedVector3D b) => a.Equals(b);
    public static bool operator !=(FixedVector3D a, FixedVector3D b) => !a.Equals(b);
    public override string ToString() => $"({X}, {Y}, {Z})";
}
