/// Ported from source/maths/Rect.h/.cpp — 0 A.D. GPL-2.0 — float screen rect.
using System;

namespace Rts0.Core;

/// <summary>Float rectangle. Port of 0 A.D. <c>CRect</c> (source/maths/Rect.h/.cpp). Mirrors MS CRect with floats for OpenGL.</summary>
public struct Rect : IEquatable<Rect>
{
    public float Left, Top, Right, Bottom;

    public Rect(float l, float t, float r, float b) { Left = l; Top = t; Right = r; Bottom = b; }
    public Rect(Vector2D pos) { Left = pos.X; Top = pos.Y; Right = pos.X; Bottom = pos.Y; }
    public Rect(Size2D size) { Left = 0; Top = 0; Right = size.Width; Bottom = size.Height; }
    public Rect(Vector2D ul, Vector2D br) { Left = ul.X; Top = ul.Y; Right = br.X; Bottom = br.Y; }
    public Rect(Vector2D pos, Size2D size) { Left = pos.X; Top = pos.Y; Right = pos.X + size.Width; Bottom = pos.Y + size.Height; }

    public float GetWidth() => Right - Left;
    public float GetHeight() => Bottom - Top;
    public Size2D GetSize() => new(Right - Left, Bottom - Top);
    public Vector2D TopLeft() => new(Left, Top);
    public Vector2D TopRight() => new(Right, Top);
    public Vector2D BottomLeft() => new(Left, Bottom);
    public Vector2D BottomRight() => new(Right, Bottom);
    public Vector2D CenterPoint() => new((Left + Right) * 0.5f, (Top + Bottom) * 0.5f);

    public bool IsEmpty => Right - Left <= 0 || Bottom - Top <= 0;
    public static explicit operator bool(Rect r) => !r.IsEmpty;

    public bool PointInside(Vector2D p) => p.X >= Left && p.X <= Right && p.Y >= Top && p.Y <= Bottom;
    public Rect Scale(float x, float y) => new(Left * x, Top * y, Right * x, Bottom * y);
    public bool IntersectWith(Rect a) => Left < a.Right && Right > a.Left && Top < a.Bottom && Bottom > a.Top;
    public Rect Intersection(Rect a)
    {
        if (!IntersectWith(a)) return new Rect();
        return new Rect(Math.Max(Left, a.Left), Math.Max(Top, a.Top), Math.Min(Right, a.Right), Math.Min(Bottom, a.Bottom));
    }

    public static Rect operator +(Rect a, Rect b) => new(a.Left + b.Left, a.Top + b.Top, a.Right + b.Right, a.Bottom + b.Bottom);
    public static Rect operator +(Rect a, Vector2D v) => new(a.Left + v.X, a.Top + v.Y, a.Right + v.X, a.Bottom + v.Y);
    public static Rect operator +(Rect a, Size2D s) => new(a.Left + s.Width, a.Top + s.Height, a.Right + s.Width, a.Bottom + s.Height);
    public static Rect operator -(Rect a, Rect b) => new(a.Left - b.Left, a.Top - b.Top, a.Right - b.Right, a.Bottom - b.Bottom);
    public static Rect operator -(Rect a, Vector2D v) => new(a.Left - v.X, a.Top - v.Y, a.Right - v.X, a.Bottom - v.Y);
    public static Rect operator -(Rect a, Size2D s) => new(a.Left - s.Width, a.Top - s.Height, a.Right - s.Width, a.Bottom - s.Height);
    public static Rect operator -(Rect a) => new(-a.Left, -a.Top, -a.Right, -a.Bottom);
    public static Rect operator +(Rect a) => a;

    public bool Equals(Rect o) => Left == o.Left && Top == o.Top && Right == o.Right && Bottom == o.Bottom;
    public override bool Equals(object obj) => obj is Rect r && Equals(r);
    public override int GetHashCode() => HashCode.Combine(Left, Top, Right, Bottom);
    public static bool operator ==(Rect a, Rect b) => a.Equals(b);
    public static bool operator !=(Rect a, Rect b) => !a.Equals(b);
    public override string ToString() => $"[{Left},{Top},{Right},{Bottom}]";
}
