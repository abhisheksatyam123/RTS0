/* Ported from source/simulation2/helpers/Pathfinding.h/.cpp — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/Pathfinding.cpp/.h — 0 A.D. GPL-2.0
/// Common pathfinding interfaces, costs, and helpers.

using System;
using System.Collections.Generic;

namespace Rts0.Core;

public struct LongPathRequest
{
    public uint Ticket;
    public Fixed X0, Z0;
    public PathGoal Goal;
    public ushort PassClass;
    public int Notify;
}

public struct ShortPathRequest
{
    public uint Ticket;
    public Fixed X0, Z0;
    public Fixed Clearance;
    public Fixed Range;
    public PathGoal Goal;
    public ushort PassClass;
    public bool AvoidMovingUnits;
    public int Group;
    public int Notify;
}

public struct Waypoint
{
    public Fixed X, Z;
    public Waypoint(Fixed x, Fixed z) { X = x; Z = z; }
}

public sealed class WaypointPath
{
    public List<Waypoint> Waypoints = new();
    public bool Empty => Waypoints.Count == 0;
    public void Clear() => Waypoints.Clear();
}

/// <summary>Encoded grid step cost — hv*65536 + d*92682 (≈sqrt2). Port of PathCost.</summary>
public readonly struct PathCost : IComparable<PathCost>, IEquatable<PathCost>
{
    private readonly uint _data;
    public PathCost(ushort hv, ushort d) => _data = (uint)(hv * 65536 + d * 92682);
    private PathCost(uint data) => _data = data;
    public static PathCost HorizVert(ushort n) => new(n, 0);
    public static PathCost Diag(ushort n) => new(0, n);
    public static PathCost operator +(PathCost a, PathCost b) => new(a._data + b._data);
    public uint ToUInt() => _data;
    public int CompareTo(PathCost other) => _data.CompareTo(other._data);
    public bool Equals(PathCost o) => _data == o._data;
    public override bool Equals(object obj) => obj is PathCost p && Equals(p);
    public override int GetHashCode() => (int)_data;
    public static bool operator <(PathCost a, PathCost b) => a._data < b._data;
    public static bool operator >(PathCost a, PathCost b) => a._data > b._data;
    public static bool operator <=(PathCost a, PathCost b) => a._data <= b._data;
    public static bool operator >=(PathCost a, PathCost b) => a._data >= b._data;
}

public static class Pathfinding
{
    public const int PassClassBits = 16;
    public static ushort MaskFromIndex(int id) => (ushort)(1u << id);
    public static readonly ushort SpecialPassClass = MaskFromIndex(PassClassBits - 1);
    // Navcell 1m, matches 0 A.D. NAVCELL_SIZE
    public static readonly Fixed NavcellSize = Fixed.FromInt(1);
    public const int NavcellSizeInt = 1;
    public const int NavcellsPerTerrainTile = 4; // TERRAIN_TILE_SIZE (4) / NAVCELL_SIZE_INT
    public static readonly Fixed ClearanceExtensionRadius = Fixed.FromInt(1);

    public static void NearestNavcell(Fixed x, Fixed z, out ushort i, out ushort j, ushort w, ushort h)
    {
        int ix = MathUtil.Clamp((x / NavcellSizeInt).ToInt_RoundToZero(), 0, w - 1);
        int jz = MathUtil.Clamp((z / NavcellSizeInt).ToInt_RoundToZero(), 0, h - 1);
        i = (ushort)ix; j = (ushort)jz;
    }

    public static void NavcellCenter(ushort i, ushort j, out Fixed x, out Fixed z)
    {
        x = Fixed.FromInt(i * 2 + 1).Multiply(NavcellSize / 2);
        z = Fixed.FromInt(j * 2 + 1).Multiply(NavcellSize / 2);
    }

    public static void TerrainTileCenter(ushort i, ushort j, out Fixed x, out Fixed z)
    {
        x = Fixed.FromInt(i * 4 + 2);
        z = Fixed.FromInt(j * 4 + 2);
    }

    /// <summary>Check line movement — port of Pathfinding::CheckLineMovement.</summary>
    public static bool CheckLineMovement(Fixed x0, Fixed z0, Fixed x1, Fixed z1, ushort passClass, Grid<ushort> grid)
    {
        NearestNavcell(x0, z0, out ushort i0, out ushort j0, grid.Width, grid.Height);
        NearestNavcell(x1, z1, out ushort i1, out ushort j1, grid.Width, grid.Height);
        int di = i0 < i1 ? 1 : i1 < i0 ? -1 : 0;
        int dj = j0 < j1 ? 1 : j1 < j0 ? -1 : 0;
        int i = i0, j = j0;
        bool onImpass = (grid.Get(i0, j0) & passClass) != 0;
        while (true)
        {
            bool passable = (grid.Get(i, j) & passClass) == 0;
            if (passable) onImpass = false; else if (!onImpass) return false;
            if (i == i1 && j == j1) return true;
            if (di == 0 || i == i1) { j += dj; continue; }
            if (dj == 0 || j == j1) { i += di; continue; }
            Fixed xia = Fixed.FromInt(i).Multiply(NavcellSize);
            Fixed xib = Fixed.FromInt(i + 1).Multiply(NavcellSize);
            Fixed zj = Fixed.FromInt(j + (dj + 1) / 2).Multiply(NavcellSize);
            FixedVector2D perp = new FixedVector2D(x1 - x0, z1 - z0);
            perp = new FixedVector2D(perp.Y, Fixed.Zero - perp.X);
            Fixed dota = new FixedVector2D(xia, zj).Dot(perp) - new FixedVector2D(x0, z0).Dot(perp);
            Fixed dotb = new FixedVector2D(xib, zj).Dot(perp) - new FixedVector2D(x0, z0).Dot(perp);
            bool bothNeg = dota < Fixed.Zero && dotb < Fixed.Zero;
            bool bothPos = dota > Fixed.Zero && dotb > Fixed.Zero;
            if (bothNeg || bothPos) i += di; else j += dj;
        }
    }
}

public sealed class PathfinderPassability
{
    public ushort Mask;
    public Fixed Clearance;
    public enum ObstructionHandling { None, Pathfinding, Foundation }
    public ObstructionHandling Obstructions;
    private Fixed _minDepth, _maxDepth, _maxSlope, _minShore, _maxShore;
    public PathfinderPassability(ushort mask, Fixed clearance = default, ObstructionHandling obs = ObstructionHandling.None)
    { Mask = mask; Clearance = clearance; Obstructions = obs; _minDepth = Fixed.FromRaw(int.MinValue); _maxDepth = Fixed.FromRaw(int.MaxValue); _maxSlope = Fixed.FromRaw(int.MaxValue); _minShore = Fixed.FromRaw(int.MinValue); _maxShore = Fixed.FromRaw(int.MaxValue); }
    public bool IsPassable(Fixed waterDepth, Fixed steepness, Fixed shoreDist)
        => _minDepth <= waterDepth && waterDepth <= _maxDepth && steepness < _maxSlope && _minShore <= shoreDist && shoreDist <= _maxShore;
}
