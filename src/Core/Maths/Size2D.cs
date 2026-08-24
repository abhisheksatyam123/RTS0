/// Ported from source/maths/Size2D.h/.cpp — 0 A.D. GPL-2.0 — 2D size.

namespace Rts0.Core;

/// <summary>2D size. Port of 0 A.D. <c>CSize2D</c> (source/maths/Size2D.h/.cpp).</summary>
public struct Size2D : IEquatable<Size2D>
{
    public float Width, Height;
    public Size2D(float w, float h) { Width = w; Height = h; }

    public static Size2D operator +(Size2D a, Size2D b) => new(a.Width + b.Width, a.Height + b.Height);
    public static Size2D operator -(Size2D a, Size2D b) => new(a.Width - b.Width, a.Height - b.Height);
    public static Size2D operator *(Size2D s, float f) => new(s.Width * f, s.Height * f);
    public static Size2D operator /(Size2D s, float f) => new(s.Width / f, s.Height / f);

    public bool Equals(Size2D o) => Width == o.Width && Height == o.Height;
    public override bool Equals(object obj) => obj is Size2D s && Equals(s);
    public override int GetHashCode() => HashCode.Combine(Width, Height);
    public static bool operator ==(Size2D a, Size2D b) => a.Equals(b);
    public static bool operator !=(Size2D a, Size2D b) => !a.Equals(b);
    public override string ToString() => $"({Width}x{Height})";
}
