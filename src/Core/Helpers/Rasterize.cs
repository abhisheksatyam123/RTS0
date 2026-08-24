/* Ported from source/simulation2/helpers/Rasterize.h/.cpp — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/Rasterize.cpp/.h — 0 A.D. GPL-2.0
/// Rasterize rotated rectangles onto Grid<byte> via span filling.

using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Span of cells [i0,i1) on row j. Mirrors SimRasterize::Span.</summary>
public struct Span
{
    public short I0, I1, J;
    public Span(short i0, short i1, short j) { I0 = i0; I1 = i1; J = j; }
}

/// <summary>Obstruction square for rasterization. Mirrors ICmpObstructionManager::ObstructionSquare.</summary>
public struct ObstructionSquare
{
    public Fixed X, Z;
    public FixedVector2D U, V;
    public Fixed Hw, Hh;
    public ObstructionSquare(Fixed x, Fixed z, FixedVector2D u, FixedVector2D v, Fixed hw, Fixed hh) { X = x; Z = z; U = u; V = v; Hw = hw; Hh = hh; }
}

public static class Rasterize
{
    /// <summary>
    /// Convert rotated rectangle + clearance into spans of strictly-inside navcells.
    /// Port of SimRasterize::RasterizeRectWithClearance. Same signature pattern as 0 A.D.
    /// </summary>
    public static void RasterizeRectWithClearance(List<Span> spans, in ObstructionSquare shape, Fixed clearance, Fixed cellSize)
    {
        spans.Clear();
        Fixed ext = clearance + Fixed.FromInt(1); // CLEARANCE_EXTENSION_RADIUS
        FixedVector2D halfSize = new(shape.Hw + ext, shape.Hh + ext);
        FixedVector2D halfBound = Geometry.GetHalfBoundingBox(shape.U, shape.V, halfSize);
        FixedVector2D shapeHalf = new(shape.Hw, shape.Hh);
        // i0 = floor((x-half)/cell), i1 = ceil((x+half)/cell)
        int i0 = FloorDiv(shape.X - halfBound.X, cellSize);
        int j0 = FloorDiv(shape.Z - halfBound.Y, cellSize);
        int i1 = CeilDiv(shape.X + halfBound.X, cellSize);
        int j1 = CeilDiv(shape.Z + halfBound.Y, cellSize);
        if (j1 <= j0) return;
        Fixed ext2 = ext.Multiply(ext);
        spans.Capacity = Math.Max(spans.Capacity, j1 - j0);
        for (int j = j0; j < j1; j++)
        {
            int spanI0 = short.MaxValue, spanI1 = short.MinValue;
            bool prevInside = false;
            bool skipNext = false;
            for (int i = i0; i < i1; i++)
            {
                if (skipNext) { skipNext = false; continue; }
                Fixed cx1 = cellSize * (i + 1) - shape.X;
                Fixed cz0 = cellSize * j - shape.Z;
                Fixed cz1 = cellSize * (j + 1) - shape.Z;
                Fixed cx0 = cellSize * i - shape.X;
                if (Geometry.DistanceToSquareSquared(new FixedVector2D(cx1, cz0), shape.U, shape.V, shapeHalf, true) > ext2)
                { if (prevInside) break; skipNext = true; continue; }
                if (Geometry.DistanceToSquareSquared(new FixedVector2D(cx1, cz1), shape.U, shape.V, shapeHalf, true) > ext2)
                { if (prevInside) break; skipNext = true; continue; }
                if (!prevInside)
                {
                    if (Geometry.DistanceToSquareSquared(new FixedVector2D(cx0, cz0), shape.U, shape.V, shapeHalf, true) > ext2) continue;
                    if (Geometry.DistanceToSquareSquared(new FixedVector2D(cx0, cz1), shape.U, shape.V, shapeHalf, true) > ext2) continue;
                    prevInside = true; spanI0 = i;
                }
                spanI1 = i + 1;
            }
            if (spanI0 < spanI1) spans.Add(new Span((short)spanI0, (short)spanI1, (short)j));
        }
    }

    /// <summary>Rasterize shape directly onto Grid&lt;byte&gt; (set to 0 = blocked). Helper over span API.</summary>
    public static void RasterizeToGrid(Grid<byte> grid, in ObstructionSquare shape, Fixed clearance, Fixed cellSize, int worldOffsetX = 0, int worldOffsetZ = 0)
    {
        var spans = new List<Span>();
        RasterizeRectWithClearance(spans, shape, clearance, cellSize);
        foreach (var s in spans)
            for (int i = s.I0; i < s.I1; i++)
            {
                int gx = i - worldOffsetX, gz = s.J - worldOffsetZ;
                if (gx >= 0 && gz >= 0 && gx < grid.Width && gz < grid.Height) grid.Set(gx, gz, 0);
            }
    }

    public static void RasterizeSpansToGrid(Grid<byte> grid, List<Span> spans, byte value = 1)
    {
        foreach (var s in spans)
            for (int i = s.I0; i < s.I1; i++)
                if (i >= 0 && s.J >= 0 && i < grid.Width && s.J < grid.Height)
                    grid.Set(i, s.J, value);
    }

    private static int FloorDiv(Fixed a, Fixed b)
    {
        int raw = (int)(((long)a.RawValue << Fixed.FractBits) / b.RawValue);
        return raw >> Fixed.FractBits;
    }
    private static int CeilDiv(Fixed a, Fixed b)
    {
        int raw = (int)(((long)a.RawValue << Fixed.FractBits) / b.RawValue);
        int floor = raw >> Fixed.FractBits;
        if (raw % Fixed.FractPow2 != 0) floor++;
        return floor;
    }
}
