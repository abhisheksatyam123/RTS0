/* Ported from source/simulation2/components/CCmpTerritoryManager.cpp + ICmpTerritoryManager.h — 0 A.D. GPL-2.0 */
/// <summary>Territory ownership via influence map — mirrors CCmpTerritoryManager.</summary>
using System;
using System.Collections.Generic;
using Godot;

namespace Rts0.Core;

public sealed class TerritoryManager : ISystemComponent
{
    public int EntityId => 0;
    public void Init(Dictionary<string, object> p) { }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { }
    public void Deserialize(System.IO.BinaryReader r) { }

    public const int NavcellsPerTerritoryTile = 8;
    public const int PlayerMask = 0x1F, ConnectedMask = 0x20, BlinkingMask = 0x40;

    private Grid<byte> _territories;
    private Grid<byte> _costGrid;
    private byte _impassableCost = 255;
    private int _w, _h;
    private readonly List<Influence> _influences = new();
    private readonly Dictionary<int, int> _cellCounts = new();
    private int _totalPassable;
    private bool _dirty = true;
    private int _dirtyId = 1, _blinkingId = 1;

    private struct Influence { public int entity, owner; public FixedVector2D pos; public uint weight, radius; public bool isRoot; }

    public void Init(int w, int h)
    {
        _w = w; _h = h;
        _costGrid = new Grid<byte>((ushort)w, (ushort)h);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) _costGrid.Set(x, y, 0);
        _territories = new Grid<byte>((ushort)w, (ushort)h);
        _dirty = true;
    }
    public void Tick(float dt) { if (_dirty) CalculateTerritories(); }
    public void AddInfluence(Vector2I cell, int owner, int weight)
    {
        var pos = new FixedVector2D(Fixed.FromInt(cell.X), Fixed.FromInt(cell.Y));
        AddInfluence(cell.X * 1000 + cell.Y, owner, pos, (uint)weight * 10, (uint)weight * 5, isRoot: true);
    }

    public void SetImpassableCost(byte c) => _impassableCost = c;

    public void SetCostGrid(Grid<byte> cost) { _costGrid = cost; _dirty = true; }

    public void AddInfluence(int entity, int owner, FixedVector2D pos, uint weight, uint radius, bool isRoot = false)
    {
        _influences.Add(new Influence { entity = entity, owner = owner, pos = pos, weight = weight, radius = radius, isRoot = isRoot });
        _dirty = true;
    }
    public void RemoveInfluence(int entity) { _influences.RemoveAll(i => i.entity == entity); _dirty = true; }

    public void UpdateInfluencePosition(int entity, FixedVector2D pos)
    {
        for (int i = 0; i < _influences.Count; i++) if (_influences[i].entity == entity) { var inf = _influences[i]; inf.pos = pos; _influences[i] = inf; _dirty = true; break; }
    }

    public Grid<byte> GetTerritoryGrid() { CalculateTerritories(); return _territories; }

    public int GetOwner(Fixed x, Fixed z)
    {
        CalculateTerritories();
        if (_territories == null) return 0;
        ushort i, j; NearestTile(x, z, out i, out j);
        return _territories.Get(i, j) & PlayerMask;
    }
    public bool IsConnected(Fixed x, Fixed z)
    {
        CalculateTerritories();
        if (_territories == null) return false;
        ushort i, j; NearestTile(x, z, out i, out j);
        return (_territories.Get(i, j) & ConnectedMask) != 0;
    }
    public bool IsTerritoryBlinking(Fixed x, Fixed z)
    {
        if (_territories == null) return false;
        ushort i, j; NearestTile(x, z, out i, out j);
        return (_territories.Get(i, j) & BlinkingMask) != 0;
    }
    public void SetTerritoryBlinking(Fixed x, Fixed z, bool enable)
    {
        CalculateTerritories();
        if (_territories == null) return;
        ushort si, sj; NearestTile(x, z, out si, out sj);
        int owner = _territories.Get(si, sj) & PlayerMask;
        var q = new Queue<(int,int)>(); q.Enqueue((si, sj));
        var seen = new HashSet<long>();
        while (q.Count > 0)
        {
            var (i, j) = q.Dequeue(); long k = ((long)i << 32) | (uint)j; if (!seen.Add(k)) continue;
            if (i < 0 || j < 0 || i >= _w || j >= _h) continue;
            if ((_territories.Get(i, j) & PlayerMask) != owner) continue;
            byte cur = _territories.Get(i, j);
            bool blink = (cur & BlinkingMask) != 0;
            if (blink != enable) { _territories.Set(i, j, (byte)(enable ? cur | BlinkingMask : cur & ~BlinkingMask)); _blinkingId++; }
            q.Enqueue((i+1,j)); q.Enqueue((i-1,j)); q.Enqueue((i,j+1)); q.Enqueue((i,j-1));
        }
    }

    public byte GetTerritoryPercentage(int player)
    {
        CalculateTerritories();
        if (_totalPassable == 0) return 0;
        return _cellCounts.TryGetValue(player, out int c) ? (byte)(c * 100 / _totalPassable) : (byte)0;
    }

    public bool NeedUpdateTexture(ref int dirtyId)
    {
        if (dirtyId == _dirtyId) return false;
        dirtyId = _dirtyId; return true;
    }

    public void CalculateTerritories()
    {
        if (!_dirty && _territories != null) return;
        if (_costGrid == null) return;
        _w = _costGrid.Width; _h = _costGrid.Height;
        _territories = new Grid<byte>((ushort)_w, (ushort)_h);
        var bestWeight = new Grid<uint>((ushort)_w, (ushort)_h);
        _cellCounts.Clear(); _totalPassable = 0;
        for (int y = 0; y < _h; y++) for (int x = 0; x < _w; x++) if (_costGrid.Get(x, y) < _impassableCost) _totalPassable++;

        // per-player influence accumulation via floodfill from each source
        foreach (var inf in _influences)
        {
            if (inf.weight == 0 || inf.radius == 0) continue;
            ushort si, sj; NearestTile(inf.pos.X, inf.pos.Y, out si, out sj);
            var q = new Queue<(int x,int y,uint w)>();
            var visited = new HashSet<long>();
            q.Enqueue((si, sj, inf.weight));
            uint falloffPer = inf.radius == 0 ? uint.MaxValue : inf.weight * (uint)NavcellsPerTerritoryTile / inf.radius;
            while (q.Count > 0)
            {
                var (x, y, w) = q.Dequeue(); if (x < 0 || y < 0 || x >= _w || y >= _h) continue;
                long k = ((long)x << 32) | (uint)y; if (!visited.Add(k)) continue;
                if (w == 0) continue;
                // add influence
                // simplified: just keep max per tile; we track bestWeight per player implicitly via max
                if (w > bestWeight.Get(x, y)) { bestWeight.Set(x, y, w); _territories.Set(x, y, (byte)inf.owner); }
                uint cost = _costGrid.Get(x, y);
                uint falloff = falloffPer * (cost == 255 ? 255 : cost);
                if (w <= falloff) continue;
                uint nw = w - falloff;
                q.Enqueue((x+1, y, nw)); q.Enqueue((x-1, y, nw)); q.Enqueue((x, y+1, nw)); q.Enqueue((x, y-1, nw));
            }
        }
        // mark connected via root influences
        foreach (var inf in _influences) if (inf.isRoot)
        {
            ushort si, sj; NearestTile(inf.pos.X, inf.pos.Y, out si, out sj);
            int owner = inf.owner;
            var q = new Queue<(int,int)>(); q.Enqueue((si, sj));
            var seen = new HashSet<long>();
            while (q.Count > 0)
            {
                var (x, y) = q.Dequeue(); long k = ((long)x << 32) | (uint)y; if (!seen.Add(k)) continue;
                if (x < 0 || y < 0 || x >= _w || y >= _h) continue;
                if ((_territories.Get(x, y) & PlayerMask) != owner) continue;
                if ((_territories.Get(x, y) & ConnectedMask) != 0) continue;
                _territories.Set(x, y, (byte)(_territories.Get(x, y) | ConnectedMask));
                _cellCounts[owner] = _cellCounts.TryGetValue(owner, out int c) ? c+1 : 1;
                q.Enqueue((x+1,y)); q.Enqueue((x-1,y)); q.Enqueue((x,y+1)); q.Enqueue((x,y-1));
            }
        }
        _dirty = false; _dirtyId++;
    }

    private void NearestTile(Fixed x, Fixed z, out ushort i, out ushort j)
    {
        int scale = NavcellsPerTerritoryTile;
        i = (ushort)MathUtil.Clamp((x / scale).ToInt_RoundToZero(), 0, _w - 1);
        j = (ushort)MathUtil.Clamp((z / scale).ToInt_RoundToZero(), 0, _h - 1);
    }

    public void SetVisibility(bool v) { }
    public bool IsVisible() => true;
    public void UpdateColors() { }
}
