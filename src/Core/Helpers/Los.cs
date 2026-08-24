/* Ported from source/simulation2/helpers/Los.h — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/Los.h — 0 A.D. GPL-2.0
/// Line-of-sight helpers. Integrates Grid + RangeManager LOS bitmask.

using System;

namespace Rts0.Core;

public enum LosState : byte { Unexplored = 0, Explored = 1, Visible = 2, Mask = 3 }

public static class Los
{
    public const int TileSize = 4; // LOS_TILE_SIZE
}

/// <summary>
/// Querier over LOS bitmask grid — port of CLosQuerier.
/// Depends on RangeManager's packed Grid&lt;uint&gt; (2 bits per player).
/// </summary>
public sealed class LosQuerier
{
    private readonly uint _playerMask;
    private readonly Grid<uint> _data;
    private readonly int _verticesPerSide;

    public LosQuerier(uint playerMask, Grid<uint> data, int verticesPerSide)
    {
        _playerMask = playerMask;
        _data = data;
        _verticesPerSide = verticesPerSide;
    }

    public static uint MaskForPlayer(int playerId) => 0b11u << (playerId * 2);

    public bool IsVisible(int i, int j)
    {
        if (i < 0 || j < 0 || i >= _verticesPerSide || j >= _verticesPerSide) return false;
        return ((_data.Get(i, j) & _playerMask) & 0xAAAAAAAAu) != 0;
    }
    public bool IsExplored(int i, int j)
    {
        if (i < 0 || j < 0 || i >= _verticesPerSide || j >= _verticesPerSide) return false;
        return ((_data.Get(i, j) & _playerMask) & 0x55555555u) != 0;
    }
    public bool IsVisible_Unchecked(int i, int j) => ((_data.Get(i, j) & _playerMask) & 0xAAAAAAAAu) != 0;
    public bool IsExplored_Unchecked(int i, int j) => ((_data.Get(i, j) & _playerMask) & 0x55555555u) != 0;

    public LosState GetState(int i, int j)
    {
        if (i < 0 || j < 0 || i >= _verticesPerSide || j >= _verticesPerSide) return LosState.Unexplored;
        uint v = _data.Get(i, j) & _playerMask;
        bool vis = (v & 0xAAAAAAAAu) != 0;
        bool exp = (v & 0x55555555u) != 0;
        if (vis) return LosState.Visible;
        if (exp) return LosState.Explored;
        return LosState.Unexplored;
    }

    // World -> LOS vertex conversion
    public static void WorldToLos(Fixed x, Fixed z, out int i, out int j)
    {
        i = (x / Los.TileSize).ToInt_RoundToZero();
        j = (z / Los.TileSize).ToInt_RoundToZero();
    }
    public bool IsVisibleWorld(Fixed x, Fixed z) { WorldToLos(x, z, out int i, out int j); return IsVisible(i, j); }
    public bool IsExploredWorld(Fixed x, Fixed z) { WorldToLos(x, z, out int i, out int j); return IsExplored(i, j); }
}

/// <summary>LOS update helper — mirrors CCmpRangeManager LOS reveal logic.</summary>
public static class LosHelper
{
    public static void RevealCircle(Grid<uint> losGrid, int cx, int cy, int radius, uint playerMask, bool visible = true)
    {
        int w = losGrid.Width, h = losGrid.Height;
        uint visBits = visible ? 0xAAAAAAAAu & playerMask : 0;
        uint expBits = 0x55555555u & playerMask;
        int r2 = radius * radius;
        for (int y = cy - radius; y <= cy + radius; y++)
        for (int x = cx - radius; x <= cx + radius; x++)
        {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r2) continue;
            uint cur = losGrid.Get(x, y);
            cur |= expBits;
            if (visible) cur |= visBits;
            losGrid.Set(x, y, cur);
        }
    }

    public static void UnrevealAll(Grid<uint> losGrid, uint playerMask)
    {
        uint invVis = ~(0xAAAAAAAAu & playerMask);
        for (int y = 0; y < losGrid.Height; y++)
        for (int x = 0; x < losGrid.Width; x++)
            losGrid.Set(x, y, losGrid.Get(x, y) & invVis);
    }

    public static bool HasLos(uint playerMask, Grid<uint> data, int verticesPerSide, int i, int j)
        => new LosQuerier(playerMask, data, verticesPerSide).IsVisible(i, j);
}
