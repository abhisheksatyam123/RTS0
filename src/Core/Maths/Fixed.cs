/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D. - ported to C# for RTS0.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Direct port of source/maths/Fixed.h (CFixed_15_16)
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

    internal Fixed(int raw) => _value = raw;
    public static Fixed FromRaw(int raw) => new(raw);

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

    public static Fixed operator +(Fixed a, Fixed b) => new(a._value + b._value);
    public static Fixed operator -(Fixed a, Fixed b) => new(a._value - b._value);
    public static Fixed operator -(Fixed a) => new(-a._value);
    public static Fixed operator *(Fixed a, int n) => new(a._value * n);
    public static Fixed operator *(Fixed a, Fixed b) => a.Multiply(b);
    public static Fixed operator /(Fixed a, int n) => new(a._value / n);
    public static Fixed operator >>(Fixed a, int n) => new(a._value >> n);
    public static Fixed operator <<(Fixed a, int n) => new(a._value << n);

    public static Fixed operator /(Fixed a, Fixed b)
    {
        long t = ((long)a._value << FractBits) / b._value;
        return new((int)t);
    }

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
