/// Ported from source/simulation2/helpers/MapEdgeTiles.h — 0 A.D. GPL-2.0
using System;

namespace Rts0.Core;

/// <summary>Map edge tiles — mirrors <c>MAP_EDGE_TILES</c> (3 impassable border tiles).</summary>
public static class MapEdgeTiles
{
    public const int Count = 3;
    public static bool IsEdgeTile(int x,int z,int mapSize) => x < Count || z < Count || x >= mapSize - Count || z >= mapSize - Count;
    public static int InnerSize(int mapSize) => Math.Max(0, mapSize - 2 * Count);
}
