/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D. - ported to C# for RTS0.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Direct port of source/maths/Fixed.h (CFixed_15_16) + source/maths/Sqrt.cpp
 * Original C++: Fixed.h ~408 lines. This C# version keeps deterministic
 * 15.16 fixed-point semantics so simulation stays bit-identical across
 * Windows/Linux/Android/iOS (host-authoritative now, lockstep-ready later).
 */

using System;

namespace Rts0.Core;

/// <summary>
/// Deterministic 15.16 fixed-point. 1 sign + 15 int + 16 fract = 32 bits.
/// Matches 0 A.D. `fixed` / `CFixed_15_16`. Use ONLY in simulation code;
/// convert to float only at the render edge (WorldView).
/// </summary>
public readonly struct Fixed : IEquatable<Fixed>, IComparable<Fixed>
{
    public const int FractBits = 16;
    public const int FractPow2 = 1 << FractBits;

    private readonly int _value;

    private Fixed(int raw) => _value = raw;

    public static Fixed Zero => new(0);
    public static Fixed One => FromInt(1);
    public static Fixed Epsilon => new(1);
    public static Fixed Pi => FromDouble(Math.PI);

    public int RawValue => _value;

    public static Fixed FromInt(int n) => new(n << FractBits);

    public static Fixed FromFloat(float n)
    {
        if (!float.IsFinite(n)) return Zero;
        return new(RoundAwayFromZero(n * FractPow2));
    }

    public static Fixed FromDouble(double n)
    {
        if (!double.IsFinite(n)) return Zero;
        return new(RoundAwayFromZero(n * FractPow2));
    }

    public float ToFloat() => _value / (float)FractPow2;
    public double ToDouble() => _value / (double)FractPow2;

    public int ToInt_RoundToZero() => _value >> FractBits;
    public int ToInt_RoundToNearest() => (_value + FractPow2 / 2) >> FractBits;

    public bool IsZero() => _value == 0;

    public Fixed Absolute() => new(Math.Abs(_value));

    // ---- arithmetic (mirrors Fixed.h, overflow checks omitted in release) ----
    public static Fixed operator +(Fixed a, Fixed b) => new(a._value + b._value);
    public static Fixed operator -(Fixed a, Fixed b) => new(a._value - b._value);
    public static Fixed operator -(Fixed a) => new(-a._value);
    public static Fixed operator *(Fixed a, int n) => new(a._value * n);
    public static Fixed operator /(Fixed a, int n) => new(a._value / n);
    public static Fixed operator >>(Fixed a, int n) => new(a._value >> n);
    public static Fixed operator <<(Fixed a, int n) => new(a._value << n);

    public static Fixed operator /(Fixed a, Fixed b)
    {
        long t = ((long)a._value << FractBits) / b._value;
        return new((int)t);
    }

    /// <summary>Multiply two fixed values (use Multiply() name to flag overflow risk, as in C++).</summary>
    public Fixed Multiply(Fixed other)
    {
        long t = (long)_value * other._value;
        t >>= FractBits;
        return new((int)t);
    }

    public Fixed MulDiv(Fixed m, Fixed d)
    {
        long t = (long)_value * m._value / d._value;
        return new((int)t);
    }

    public Fixed Sqrt()
    {
        if (_value <= 0) return Zero;
        uint s = SqrtUtil.Isqrt64((ulong)_value << FractBits);
        return new((int)s);
    }

    // ---- comparisons ----
    public static bool operator ==(Fixed a, Fixed b) => a._value == b._value;
    public static bool operator !=(Fixed a, Fixed b) => a._value != b._value;
    public static bool operator <(Fixed a, Fixed b) => a._value < b._value;
    public static bool operator >(Fixed a, Fixed b) => a._value > b._value;
    public static bool operator <=(Fixed a, Fixed b) => a._value <= b._value;
    public static bool operator >=(Fixed a, Fixed b) => a._value >= b._value;

    public bool Equals(Fixed other) => _value == other._value;
    public override bool Equals(object obj) => obj is Fixed f && Equals(f);
    public override int GetHashCode() => _value;
    public int CompareTo(Fixed other) => _value.CompareTo(other._value);
    public override string ToString() => ToFloat().ToString("F4");

    private static int RoundAwayFromZero(float v) => (int)(v >= 0 ? v + 0.5f : v - 0.5f);
    private static int RoundAwayFromZero(double v) => (int)(v >= 0 ? v + 0.5 : v - 0.5);
}

/// <summary>Deterministic integer sqrt — direct port of source/maths/Sqrt.cpp isqrt64.</summary>
internal static class SqrtUtil
{
    public static uint Isqrt64(ulong n)
    {
        ulong op = n;
        ulong res = 0;
        ulong one = 1UL << 62;
        while (one > op) one >>= 2;
        while (one != 0)
        {
            if (op >= res + one)
            {
                op -= res + one;
                res += one << 1;
            }
            res >>= 1;
            one >>= 2;
        }
        return (uint)res;
    }
}

/// <summary>Direct port of source/maths/FixedVector2D.h — deterministic 2D vector.</summary>
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

    public void Normalize()
    {
        // Note: struct is readonly, so Normalize returns new vector in practice via copy.
        // Kept as method for API parity; callers should use Normalized().
    }

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
