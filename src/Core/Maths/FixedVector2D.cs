/* Ported from source/maths/FixedVector2D.h — deterministic 2D vector. */

using System;

namespace Rts0.Core;

public readonly struct FixedVector2D : IEquatable<FixedVector2D>
{
    public readonly Fixed X;
    public readonly Fixed Y;

    public FixedVector2D(Fixed x, Fixed y) { X = x; Y = y; }

    public static FixedVector2D operator +(FixedVector2D a, FixedVector2D b) => new(a.X + b.X, a.Y + b.Y);
    public static FixedVector2D operator -(FixedVector2D a, FixedVector2D b) => new(a.X - b.X, a.Y - b.Y);
    public static FixedVector2D operator -(FixedVector2D v) => new(-v.X, -v.Y);
    public static FixedVector2D operator *(FixedVector2D v, int n) => new(v.X * n, v.Y * n);
    public static FixedVector2D operator /(FixedVector2D v, int n) => new(v.X / n, v.Y / n);

    public FixedVector2D Multiply(Fixed n) => new(X.Multiply(n), Y.Multiply(n));

    public Fixed Length()
    {
        ulong xx = (ulong)(long)X.RawValue * (ulong)(long)X.RawValue;
        ulong yy = (ulong)(long)Y.RawValue * (ulong)(long)Y.RawValue;
        ulong d2 = xx + yy;
        uint d = SqrtUtil.Isqrt64(d2);
        return new Fixed((int)d);
    }

    public int CompareLength(Fixed cmp)
    {
        ulong d2 = (ulong)(long)X.RawValue * (ulong)(long)X.RawValue + (ulong)(long)Y.RawValue * (ulong)(long)Y.RawValue;
        ulong cmp2 = (ulong)(long)cmp.RawValue * (ulong)(long)cmp.RawValue;
        if (d2 < cmp2) return -1;
        if (d2 > cmp2) return 1;
        return 0;
    }

    public bool IsZero() => X.IsZero() && Y.IsZero();

    public FixedVector2D Normalized()
    {
        if (IsZero()) return this;
        Fixed len = Length();
        return new FixedVector2D(X / len, Y / len);
    }

    public FixedVector2D Normalized(Fixed targetLen)
    {
        Fixed len = Length();
        if (len.IsZero()) return this;
        return new FixedVector2D(X.MulDiv(targetLen, len), Y.MulDiv(targetLen, len));
    }

    public Fixed Dot(FixedVector2D v)
    {
        long x = (long)X.RawValue * v.X.RawValue;
        long y = (long)Y.RawValue * v.Y.RawValue;
        long sum = x + y;
        sum >>= Fixed.FractBits;
        return new Fixed((int)sum);
    }

    public bool Equals(FixedVector2D other) => X == other.X && Y == other.Y;
    public override bool Equals(object obj) => obj is FixedVector2D v && Equals(v);
    public override int GetHashCode() => HashCode.Combine(X, Y);
    public static bool operator ==(FixedVector2D a, FixedVector2D b) => a.Equals(b);
    public static bool operator !=(FixedVector2D a, FixedVector2D b) => !a.Equals(b);
}
