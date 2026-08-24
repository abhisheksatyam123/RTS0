/* Ported from source/simulation2/helpers/Geometry.h/.cpp — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/Geometry.cpp/.h — 0 A.D. GPL-2.0
/// Deterministic Fixed geometry helpers. Mirrors Geometry:: namespace.

using System;

namespace Rts0.Core;

public static class Geometry
{
    // helpers for FixedVector2D missing ops
    private static FixedVector2D Perp(FixedVector2D v) => new(v.Y, Fixed.Zero - v.X);
    private static Fixed Cross(FixedVector2D a, FixedVector2D b) => Fixed.FromRaw((int)(((long)a.X.RawValue * b.Y.RawValue - (long)a.Y.RawValue * b.X.RawValue) >> Fixed.FractBits));
    private static Fixed RelOrient(FixedVector2D a, FixedVector2D b) => Cross(a, b);
    private static Fixed Square(Fixed f) => f.Multiply(f);

    /// <summary>Point inside rotated rectangle (±u*halfSize.X ±v*halfSize.Y). See PointIsInSquare.</summary>
    public static bool PointIsInSquare(FixedVector2D point, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize)
        => point.Dot(u).Absolute() <= halfSize.X && point.Dot(v).Absolute() <= halfSize.Y;

    public static FixedVector2D GetHalfBoundingBox(FixedVector2D u, FixedVector2D v, FixedVector2D halfSize)
        => new(u.X.Multiply(halfSize.X).Absolute() + v.X.Multiply(halfSize.Y).Absolute(),
               u.Y.Multiply(halfSize.X).Absolute() + v.Y.Multiply(halfSize.Y).Absolute());

    public static Fixed DistanceToSquare(FixedVector2D point, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize, bool countInsideAsZero = false)
    {
        Fixed du = point.Dot(u).Absolute(), dv = point.Dot(v).Absolute();
        Fixed hw = halfSize.X, hh = halfSize.Y;
        if (du < hw) { if (dv < hh) return countInsideAsZero ? Fixed.Zero : (hw - du) < (hh - dv) ? hw - du : hh - dv; else return dv - hh; }
        else if (dv < hh) return du - hw;
        else return new FixedVector2D(du - hw, dv - hh).Length();
    }

    public static Fixed DistanceToSquareSquared(FixedVector2D point, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize, bool countInsideAsZero = false)
    {
        Fixed du = point.Dot(u).Absolute(), dv = point.Dot(v).Absolute();
        Fixed hw = halfSize.X, hh = halfSize.Y;
        if (du < hw) { if (dv < hh) return countInsideAsZero ? Fixed.Zero : Square(hw - du) < Square(hh - dv) ? Square(hw - du) : Square(hh - dv); else return Square(dv - hh); }
        else if (dv < hh) return Square(du - hw);
        else return Square(du - hw) + Square(dv - hh);
    }

    public static FixedVector2D NearestPointOnSquare(FixedVector2D point, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize)
    {
        Fixed du = point.Dot(u), dv = point.Dot(v);
        Fixed hw = halfSize.X, hh = halfSize.Y;
        if (-hw < du && du < hw)
        {
            if (-hh < dv && dv < hh && (du.Absolute() - hw).Absolute() < (dv.Absolute() - hh).Absolute())
                return du >= Fixed.Zero ? u.Multiply(hw) + v.Multiply(dv) : new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) + v.Multiply(dv);
            else
                return dv >= Fixed.Zero ? v.Multiply(hh) + u.Multiply(du) : new FixedVector2D(Fixed.Zero - v.X, Fixed.Zero - v.Y).Multiply(hh) + u.Multiply(du);
        }
        else if (-hh < dv && dv < hh)
            return du >= Fixed.Zero ? u.Multiply(hw) + v.Multiply(dv) : new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) + v.Multiply(dv);
        else
        {
            FixedVector2D corner = default;
            corner = du < Fixed.Zero ? corner - u.Multiply(hw) : corner + u.Multiply(hw);
            corner = dv < Fixed.Zero ? corner - v.Multiply(hh) : corner + v.Multiply(hh);
            return corner;
        }
    }

    public static Fixed DistanceSquareToSquare(FixedVector2D relPos, FixedVector2D u1, FixedVector2D v1, FixedVector2D halfSize1, FixedVector2D u2, FixedVector2D v2, FixedVector2D halfSize2)
    {
        Fixed hw1 = halfSize1.X, hh1 = halfSize1.Y, hw2 = halfSize2.X, hh2 = halfSize2.Y;
        if (TestRaySquare(relPos + u1.Multiply(hw1) + v1.Multiply(hh1), relPos - u1.Multiply(hw1) + v1.Multiply(hh1), u2, v2, halfSize2) ||
            TestRaySquare(relPos + u1.Multiply(hw1) + v1.Multiply(hh1), relPos + u1.Multiply(hw1) - v1.Multiply(hh1), u2, v2, halfSize2))
            return Fixed.Zero;
        Fixed d0 = DistanceToSquare(relPos + u1.Multiply(hw1) + v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed d1 = DistanceToSquare(relPos + u1.Multiply(hw1) - v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed d2 = DistanceToSquare(relPos - u1.Multiply(hw1) + v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed d3 = DistanceToSquare(relPos - u1.Multiply(hw1) - v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed d4 = DistanceToSquare(relPos + u2.Multiply(hw2) + v2.Multiply(hh2), u1, v1, halfSize1, true);
        Fixed d5 = DistanceToSquare(relPos + u2.Multiply(hw2) - v2.Multiply(hh2), u1, v1, halfSize1, true);
        Fixed d6 = DistanceToSquare(relPos - u2.Multiply(hw2) + v2.Multiply(hh2), u1, v1, halfSize1, true);
        Fixed d7 = DistanceToSquare(relPos - u2.Multiply(hw2) - v2.Multiply(hh2), u1, v1, halfSize1, true);
        Fixed m = d0; if (d1 < m) m = d1; if (d2 < m) m = d2; if (d3 < m) m = d3; if (d4 < m) m = d4; if (d5 < m) m = d5; if (d6 < m) m = d6; if (d7 < m) m = d7; return m;
    }

    public static Fixed MaxDistanceToSquare(FixedVector2D point, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize, bool countInsideAsZero = false)
    {
        Fixed hw = halfSize.X, hh = halfSize.Y;
        if (point.Dot(u).Absolute() < hw && point.Dot(v).Absolute() < hh && countInsideAsZero) return Fixed.Zero;
        Fixed l0 = (point + u.Multiply(hw) + v.Multiply(hh)).Length();
        Fixed l1 = (point + u.Multiply(hw) - v.Multiply(hh)).Length();
        Fixed l2 = (point - u.Multiply(hw) + v.Multiply(hh)).Length();
        Fixed l3 = (point - u.Multiply(hw) - v.Multiply(hh)).Length();
        Fixed m = l0; if (l1 > m) m = l1; if (l2 > m) m = l2; if (l3 > m) m = l3; return m;
    }

    public static Fixed MaxDistanceSquareToSquare(FixedVector2D relPos, FixedVector2D u1, FixedVector2D v1, FixedVector2D halfSize1, FixedVector2D u2, FixedVector2D v2, FixedVector2D halfSize2)
    {
        Fixed hw1 = halfSize1.X, hh1 = halfSize1.Y;
        Fixed m0 = MaxDistanceToSquare(relPos + u1.Multiply(hw1) + v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed m1 = MaxDistanceToSquare(relPos + u1.Multiply(hw1) - v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed m2 = MaxDistanceToSquare(relPos - u1.Multiply(hw1) + v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed m3 = MaxDistanceToSquare(relPos - u1.Multiply(hw1) - v1.Multiply(hh1), u2, v2, halfSize2, true);
        Fixed m = m0; if (m1 > m) m = m1; if (m2 > m) m = m2; if (m3 > m) m = m3; return m;
    }

    public static bool TestRaySquare(FixedVector2D a, FixedVector2D b, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize)
    {
        Fixed hw = halfSize.X, hh = halfSize.Y;
        Fixed au = a.Dot(u), av = a.Dot(v);
        if (-hw <= au && au <= hw && -hh <= av && av <= hh) return false;
        Fixed bu = b.Dot(u), bv = b.Dot(v);
        if (-hw <= bu && bu <= hw && -hh <= bv && bv <= hh) return true;
        if ((au < -hw && bu < -hw) || (au > hw && bu > hw) || (av < -hh && bv < -hh) || (av > hh && bv > hh)) return false;
        FixedVector2D abp = Perp(b - a);
        Fixed s0 = abp.Dot(u.Multiply(hw) + v.Multiply(hh) - a);
        Fixed s1 = abp.Dot(u.Multiply(hw) - v.Multiply(hh) - a);
        Fixed s2 = abp.Dot(new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) - v.Multiply(hh) - a);
        Fixed s3 = abp.Dot(new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) + v.Multiply(hh) - a);
        if (s0.IsZero() || s1.IsZero() || s2.IsZero() || s3.IsZero()) return true;
        bool sign = s0 < Fixed.Zero;
        return (s1 < Fixed.Zero) != sign || (s2 < Fixed.Zero) != sign || (s3 < Fixed.Zero) != sign;
    }

    public static bool TestRayAASquare(FixedVector2D a, FixedVector2D b, FixedVector2D halfSize)
    {
        Fixed hw = halfSize.X, hh = halfSize.Y;
        if (-hw <= a.X && a.X <= hw && -hh <= a.Y && a.Y <= hh) return false;
        if (-hw <= b.X && b.X <= hw && -hh <= b.Y && b.Y <= hh) return true;
        if ((a.X < -hw && b.X < -hw) || (a.X > hw && b.X > hw) || (a.Y < -hh && b.Y < -hh) || (a.Y > hh && b.Y > hh)) return false;
        FixedVector2D abp = Perp(b - a);
        Fixed s0 = abp.Dot(new FixedVector2D(hw, hh) - a);
        Fixed s1 = abp.Dot(new FixedVector2D(hw, Fixed.Zero - hh) - a);
        Fixed s2 = abp.Dot(new FixedVector2D(Fixed.Zero - hw, Fixed.Zero - hh) - a);
        Fixed s3 = abp.Dot(new FixedVector2D(Fixed.Zero - hw, hh) - a);
        if (s0.IsZero() || s1.IsZero() || s2.IsZero() || s3.IsZero()) return true;
        bool sign = s0 < Fixed.Zero;
        return (s1 < Fixed.Zero) != sign || (s2 < Fixed.Zero) != sign || (s3 < Fixed.Zero) != sign;
    }

    private static bool SquareSat(FixedVector2D a, FixedVector2D axis, FixedVector2D u, FixedVector2D v, FixedVector2D halfSize)
    {
        Fixed hw = halfSize.X, hh = halfSize.Y;
        FixedVector2D p = Perp(axis);
        if (RelOrient(p, u.Multiply(hw) + v.Multiply(hh) - a) <= Fixed.Zero) return true;
        if (RelOrient(p, u.Multiply(hw) - v.Multiply(hh) - a) <= Fixed.Zero) return true;
        if (RelOrient(p, new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) - v.Multiply(hh) - a) <= Fixed.Zero) return true;
        if (RelOrient(p, new FixedVector2D(Fixed.Zero - u.X, Fixed.Zero - u.Y).Multiply(hw) + v.Multiply(hh) - a) <= Fixed.Zero) return true;
        return false;
    }

    public static bool TestSquareSquare(FixedVector2D c0, FixedVector2D u0, FixedVector2D v0, FixedVector2D halfSize0, FixedVector2D c1, FixedVector2D u1, FixedVector2D v1, FixedVector2D halfSize1)
    {
        FixedVector2D ca = c0 + u0.Multiply(halfSize0.X) + v0.Multiply(halfSize0.Y);
        FixedVector2D cb = c0 - u0.Multiply(halfSize0.X) - v0.Multiply(halfSize0.Y);
        FixedVector2D ca1 = c1 + u1.Multiply(halfSize1.X) + v1.Multiply(halfSize1.Y);
        FixedVector2D cb1 = c1 - u1.Multiply(halfSize1.X) - v1.Multiply(halfSize1.Y);
        if (!SquareSat(ca - c1, new FixedVector2D(Fixed.Zero - u0.X, Fixed.Zero - u0.Y), u1, v1, halfSize1)) return false;
        if (!SquareSat(ca - c1, v0, u1, v1, halfSize1)) return false;
        if (!SquareSat(cb - c1, u0, u1, v1, halfSize1)) return false;
        if (!SquareSat(cb - c1, new FixedVector2D(Fixed.Zero - v0.X, Fixed.Zero - v0.Y), u1, v1, halfSize1)) return false;
        if (!SquareSat(ca1 - c0, new FixedVector2D(Fixed.Zero - u1.X, Fixed.Zero - u1.Y), u0, v0, halfSize0)) return false;
        if (!SquareSat(ca1 - c0, v1, u0, v0, halfSize0)) return false;
        if (!SquareSat(cb1 - c0, u1, u0, v0, halfSize0)) return false;
        if (!SquareSat(cb1 - c0, new FixedVector2D(Fixed.Zero - v1.X, Fixed.Zero - v1.Y), u0, v0, halfSize0)) return false;
        return true;
    }

    public static int GetPerimeterDistance(int xMax, int yMax, int x, int y)
    {
        if (xMax <= 0 || yMax <= 0) return 0;
        int q = xMax + yMax;
        if (x == xMax && y >= 0) return y;
        if (y == yMax) return q - x;
        if (x == -xMax) return 2 * q - y;
        if (y == -yMax) return 3 * q + x;
        if (x == xMax) return 4 * q + y;
        return 0;
    }

    public static (int x, int y) GetPerimeterCoordinates(int xMax, int yMax, int k)
    {
        if (xMax <= 0 || yMax <= 0) return (0, 0);
        int q = xMax + yMax; k %= 4 * q; if (k < 0) k += 4 * q;
        if (k < yMax) return (xMax, k);
        if (k < q + xMax) return (q - k, yMax);
        if (k < 2 * q + yMax) return (-xMax, 2 * q - k);
        if (k < 3 * q + xMax) return (k - 3 * q, -yMax);
        return (xMax, k - 4 * q);
    }

    public static Fixed DistanceToSegment(FixedVector2D point, FixedVector2D a, FixedVector2D b)
    {
        FixedVector2D dir = b - a;
        Fixed pd = dir.Dot(point), ad = dir.Dot(a);
        if (pd <= ad) return (point - a).Length();
        Fixed bd = dir.Dot(b);
        if (pd >= bd) return (point - b).Length();
        FixedVector2D n = Perp(dir);
        // normalize
        Fixed len = n.Length();
        if (len.IsZero()) return Fixed.Zero;
        n = new FixedVector2D(n.X.MulDiv(Fixed.One, len), n.Y.MulDiv(Fixed.One, len));
        return (n.Dot(a) - n.Dot(point)).Absolute();
    }
}
