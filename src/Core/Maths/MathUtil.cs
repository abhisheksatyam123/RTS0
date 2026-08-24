/* Ported from source/maths/MathUtil.h — scalar math utilities. */

using System;

namespace Rts0.Core;

/// <summary>
/// Render/simulation-agnostic math helpers.
/// Direct port of 0 A.D. source/maths/MathUtil.h macros/templates:
/// DEGTORAD, RADTODEG, SQR, Interpolate/Lerp, Clamp, SmoothStep, Sign.
/// </summary>
public static class MathUtil
{
    public const float PiF = MathF.PI;

    // --- Deg/Rad ---
    /// <summary>Degrees to radians. See DEGTORAD macro.</summary>
    public static float DegToRad(float a) => a * (MathF.PI / 180f);
    public static double DegToRad(double a) => a * (Math.PI / 180.0);
    /// <summary>Radians to degrees. See RADTODEG macro.</summary>
    public static float RadToDeg(float a) => a * (180f / MathF.PI);
    public static double RadToDeg(double a) => a * (180.0 / Math.PI);

    // --- Sqr ---
    /// <summary>SQR macro.</summary>
    public static float Sqr(float x) => x * x;
    public static double Sqr(double x) => x * x;
    public static int Sqr(int x) => x * x;
    public static Fixed Sqr(Fixed x) => x.Multiply(x);

    // --- Clamp ---
    /// <summary>Clamp value to [min,max]. See MathUtil.h Clamp.</summary>
    public static T Clamp<T>(T value, T min, T max) where T : IComparable<T>
    {
        if (value.CompareTo(min) <= 0) return min;
        if (value.CompareTo(max) >= 0) return max;
        return value;
    }

    public static float Clamp(float v, float min, float max) => v <= min ? min : v >= max ? max : v;
    public static double Clamp(double v, double min, double max) => v <= min ? min : v >= max ? max : v;
    public static int Clamp(int v, int min, int max) => v <= min ? min : v >= max ? max : v;
    public static Fixed Clamp(Fixed v, Fixed min, Fixed max) => v <= min ? min : v >= max ? max : v;

    // --- Interpolate / Lerp ---
    /// <summary>Linear interpolate. See MathUtil.h Interpolate.</summary>
    public static float Lerp(float a, float b, float t) => a + (b - a) * t;
    public static double Lerp(double a, double b, double t) => a + (b - a) * t;
    public static Fixed Lerp(Fixed a, Fixed b, Fixed t) => a + (b - a).Multiply(t);

    /// <summary>Alias for Lerp.</summary>
    public static float Interpolate(float a, float b, float t) => Lerp(a, b, t);
    public static double Interpolate(double a, double b, double t) => Lerp(a, b, t);
    public static Fixed Interpolate(Fixed a, Fixed b, Fixed t) => Lerp(a, b, t);

    public static T Interpolate<T>(T a, T b, float t) where T : IComparable<T> => throw new NotSupportedException("Use float/double/Fixed overload");

    // --- SmoothStep ---
    /// <summary>SmoothStep. See MathUtil.h SmoothStep.</summary>
    public static float SmoothStep(float edge0, float edge1, float value)
    {
        float x = Clamp((value - edge0) / (edge1 - edge0), 0f, 1f);
        return x * x * (3f - 2f * x);
    }
    public static double SmoothStep(double edge0, double edge1, double value)
    {
        double x = Clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }

    // --- Sign ---
    /// <summary>Sign. See MathUtil.h Sign.</summary>
    public static int Sign(int v) => v > 0 ? 1 : v < 0 ? -1 : 0;
    public static float Sign(float v) => v > 0f ? 1f : v < 0f ? -1f : 0f;
    public static double Sign(double v) => v > 0 ? 1 : v < 0 ? -1 : 0;
    public static int Sign(Fixed v) => v > Fixed.Zero ? 1 : v < Fixed.Zero ? -1 : 0;

    /// <summary>Generic sign.</summary>
    public static int Sign<T>(T value) where T : IComparable<T>
    {
        if (value.CompareTo(default(T)) > 0) return 1;
        if (value.CompareTo(default(T)) < 0) return -1;
        return 0;
    }
}
