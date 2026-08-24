/* Ported from source/simulation2/helpers/Selection.h/.cpp — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/Selection.cpp/.h — 0 A.D. GPL-2.0
/// Selection helpers — filter by owning player, LOS, template, rect.

using System;
using System.Collections.Generic;

namespace Rts0.Core;

public static class Selection
{
    public const int InvalidPlayer = -1;

    /// <summary>Filter units by owning player. Mirrors PickEntitiesInRect owner check.</summary>
    public static List<SimUnit> FilterByPlayer(IEnumerable<SimUnit> units, int owner)
    {
        var ret = new List<SimUnit>();
        foreach (var u in units)
            if (owner == InvalidPlayer || (int)u.Team == owner) ret.Add(u);
        return ret;
    }

    /// <summary>Filter non-Gaia (non-neutral) units.</summary>
    public static List<SimUnit> FilterNonGaia(IEnumerable<SimUnit> units)
    {
        var ret = new List<SimUnit>();
        foreach (var u in units) if ((int)u.Team != InvalidPlayer) ret.Add(u);
        return ret;
    }

    /// <summary>Check if world position is inside screen rect (world-space rect for RTS0).</summary>
    public static bool IsInRect(SimUnit unit, float x0, float z0, float x1, float z1)
    {
        if (x0 > x1) (x0, x1) = (x1, x0);
        if (z0 > z1) (z0, z1) = (z1, z0);
        float x = unit.Pos.X, z = unit.Pos.Z;
        return x >= x0 && x <= x1 && z >= z0 && z <= z1;
    }

    /// <summary>Pick entities in axis-aligned world rect. Mirrors PickEntitiesInRect.</summary>
    public static List<int> PickEntitiesInRect(IEnumerable<SimUnit> units, float x0, float z0, float x1, float z1, int owner, Func<SimUnit,bool>? losVisible = null)
    {
        if (x0 > x1) (x0, x1) = (x1, x0);
        if (z0 > z1) (z0, z1) = (z1, z0);
        var ret = new List<int>();
        foreach (var u in units)
        {
            if (owner != InvalidPlayer && (int)u.Team != owner) continue;
            if (losVisible != null && !losVisible(u)) continue;
            if (IsInRect(u, x0, z0, x1, z1)) ret.Add(u.Id);
        }
        return ret;
    }

    public static List<int> PickNonGaiaInRect(IEnumerable<SimUnit> units, float x0, float z0, float x1, float z1, Func<SimUnit,bool>? visible = null)
    {
        if (x0 > x1) (x0, x1) = (x1, x0);
        if (z0 > z1) (z0, z1) = (z1, z0);
        var ret = new List<int>();
        foreach (var u in units)
        {
            if ((int)u.Team == InvalidPlayer) continue;
            if (visible != null && !visible(u)) continue;
            if (IsInRect(u, x0, z0, x1, z1)) ret.Add(u.Id);
        }
        return ret;
    }

    /// <summary>Pick similar by template/selection group. Mirrors PickSimilarEntities.</summary>
    public static List<int> PickSimilar(IEnumerable<SimUnit> units, string templateName, int owner, bool matchRank, Func<SimUnit,string>? getTemplate, Func<SimUnit,string>? getGroup)
    {
        var ret = new List<int>();
        foreach (var u in units)
        {
            if (owner != InvalidPlayer && (int)u.Team != owner) continue;
            if (matchRank)
            {
                string cur = getTemplate?.Invoke(u) ?? "";
                bool matches = cur == templateName || (cur.StartsWith("foundation|") && cur.Substring(11) == templateName);
                if (!matches) continue;
            }
            else
            {
                string grp = getGroup?.Invoke(u) ?? "";
                if (grp != templateName) continue;
            }
            ret.Add(u.Id);
        }
        return ret;
    }

    /// <summary>Pick closest entity at point within radius. Mirrors PickEntityAtPoint.</summary>
    public static int PickEntityAtPoint(IEnumerable<SimUnit> units, float x, float z, float radius = 1.0f, int player = InvalidPlayer, Func<SimUnit,bool>? losVisible = null)
    {
        float r2 = radius * radius;
        float bestD2 = float.MaxValue; int best = -1;
        foreach (var u in units)
        {
            if (player != InvalidPlayer && losVisible != null && !losVisible(u)) continue;
            float dx = u.Pos.X - x, dz = u.Pos.Z - z;
            float d2 = dx * dx + dz * dz;
            if (d2 <= r2 && d2 < bestD2) { bestD2 = d2; best = u.Id; }
        }
        return best;
    }

    public static List<int> GetEntitiesWithComponent<T>(IEnumerable<SimUnit> units, Func<SimUnit,T?> getter, Func<T,bool>? filter = null) where T : class
    {
        var ret = new List<int>();
        foreach (var u in units)
        {
            var c = getter(u);
            if (c == null) continue;
            if (filter != null && !filter(c)) continue;
            ret.Add(u.Id);
        }
        return ret;
    }
}
