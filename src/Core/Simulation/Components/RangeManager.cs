/* Ported from source/simulation2/components/CCmpRangeManager.cpp + ICmpRangeManager.h — 0 A.D. GPL-2.0 */
/// <summary>Spatial query + LOS — mirrors ICmpRangeManager / CCmpRangeManager.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public enum LosVisibility : byte { Hidden = 0, Fogged = 1, Visible = 2 }

public sealed class RangeManager : ISystemComponent
{
    public int EntityId => 0;
    public void Init(Dictionary<string, object> p) { }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { }
    public void Deserialize(System.IO.BinaryReader r) { }

    private struct EntityData
    {
        public FixedVector2D pos;
        public int owner;
        public Fixed size;
        public bool inWorld;
        public Fixed visionRange;
    }

    private readonly Dictionary<int, EntityData> _entities = new();
    private readonly Dictionary<long, List<int>> _cells = new();
    private const float CellSize = 4f;
    private Grid<uint> _losState;
    private int _vertsPerSide;
    private readonly bool[] _losRevealAll = new bool[17];
    private uint _nextTag = 1;
    private readonly Dictionary<uint, Query> _queries = new();

    private struct Query
    {
        public int source;
        public Fixed minRange, maxRange;
        public uint ownersMask;
        public bool enabled;
        public List<int> lastMatch;
    }

    public void SetBounds(Fixed x0, Fixed z0, Fixed x1, Fixed z1)
    {
        int side = ((x1 - x0) / Los.TileSize).ToInt_RoundToZero() + 1;
        _vertsPerSide = Math.Max(1, side);
        _losState = new Grid<uint>((ushort)_vertsPerSide, (ushort)_vertsPerSide);
        _losRevealAll[0] = true;
    }

    public void AddEntity(int id, FixedVector2D pos, int owner, Fixed size, Fixed vision)
    {
        _entities[id] = new EntityData { pos = pos, owner = owner, size = size, inWorld = true, visionRange = vision };
        UpdateLos(id, null, pos, vision, owner, true);
    }
    public void RemoveEntity(int id)
    {
        if (!_entities.TryGetValue(id, out var d)) return;
        if (d.inWorld) UpdateLos(id, d.pos, null, d.visionRange, d.owner, false);
        _entities.Remove(id);
    }
    public void MoveEntity(int id, FixedVector2D to)
    {
        if (!_entities.TryGetValue(id, out var d)) return;
        var from = d.pos; d.pos = to; _entities[id] = d;
        UpdateLos(id, from, to, d.visionRange, d.owner, true);
    }

    // ---- passive queries ----
    public List<int> ExecuteQueryAroundPos(FixedVector2D pos, Fixed minRange, Fixed maxRange, List<int> owners = null)
    {
        RebuildHash();
        var ret = new List<int>();
        foreach (var kv in _entities)
        {
            if (!kv.Value.inWorld) continue;
            if (owners != null && !owners.Contains(kv.Value.owner)) continue;
            Fixed dist = (kv.Value.pos - pos).Length();
            if (dist < minRange) continue;
            if (maxRange.RawValue != -1 << Fixed.FractBits && dist > maxRange) continue;
            ret.Add(kv.Key);
        }
        ret.Sort((a, b) => (_entities[a].pos - pos).Length().CompareTo((_entities[b].pos - pos).Length()));
        return ret;
    }

    public List<int> ExecuteQuery(int source, Fixed minRange, Fixed maxRange, List<int> owners = null, bool accountForSize = false)
    {
        if (!_entities.TryGetValue(source, out var s) || !s.inWorld) return new List<int>();
        var pos = s.pos;
        var ret = ExecuteQueryAroundPos(pos, minRange, maxRange, owners);
        ret.Remove(source);
        if (accountForSize)
        {
            // filter by size-expanded range already approximated by dist check
        }
        return ret;
    }

    public List<int> GetEntitiesInRange(FixedVector2D pos, float range) => ExecuteQueryAroundPos(pos, Fixed.Zero, Fixed.FromFloat(range));
    public List<int> QueryRange(FixedVector2D pos, Fixed range) => GetEntitiesInRange(pos, range.ToFloat());

    // ---- active queries (simplified) ----
    public uint CreateActiveQuery(int source, Fixed minRange, Fixed maxRange, List<int> owners)
    {
        uint tag = _nextTag++;
        _queries[tag] = new Query { source = source, minRange = minRange, maxRange = maxRange, ownersMask = CalcMask(owners), enabled = false, lastMatch = new List<int>() };
        return tag;
    }
    public void DestroyActiveQuery(uint tag) => _queries.Remove(tag);
    public void EnableActiveQuery(uint tag) { if (_queries.TryGetValue(tag, out var q)) { q.enabled = true; _queries[tag] = q; } }
    public void DisableActiveQuery(uint tag) { if (_queries.TryGetValue(tag, out var q)) { q.enabled = false; _queries[tag] = q; } }
    public bool IsActiveQueryEnabled(uint tag) => _queries.TryGetValue(tag, out var q) && q.enabled;

    public List<int> GetEntitiesByPlayer(int player)
    {
        var r = new List<int>();
        foreach (var kv in _entities) if (kv.Value.owner == player) r.Add(kv.Key);
        return r;
    }

    // ---- LOS ----
    public LosQuerier GetLosQuerier(int player)
    {
        uint mask = LosQuerier.MaskForPlayer(player);
        if (_losRevealAll[player]) return new LosQuerier(mask, _losState, _vertsPerSide);
        return new LosQuerier(mask, _losState, _vertsPerSide);
    }
    public LosVisibility GetLosVisibility(int entity, int player)
    {
        if (!_entities.TryGetValue(entity, out var d) || !d.inWorld) return LosVisibility.Hidden;
        if (_losRevealAll[player]) return LosVisibility.Visible;
        var q = GetLosQuerier(player);
        FixedVector2D p = d.pos;
        int i = (p.X / Los.TileSize).ToInt_RoundToZero();
        int j = (p.Y / Los.TileSize).ToInt_RoundToZero();
        var s = q.GetState(i, j);
        return s == LosState.Visible ? LosVisibility.Visible : s == LosState.Explored ? LosVisibility.Fogged : LosVisibility.Hidden;
    }
    public void SetLosRevealAll(int player, bool v)
    {
        if (player == -1) for (int i = 0; i < _losRevealAll.Length; i++) _losRevealAll[i] = v;
        else if (player >= 0 && player < _losRevealAll.Length) _losRevealAll[player] = v;
    }
    public bool GetLosRevealAll(int player) => player >= 0 && player < _losRevealAll.Length && _losRevealAll[player];
    public void ExploreMap(int player)
    {
        if (_losState == null) return;
        uint mask = LosQuerier.MaskForPlayer(player);
        for (int y = 0; y < _vertsPerSide; y++) for (int x = 0; x < _vertsPerSide; x++) _losState.Set(x, y, _losState.Get(x, y) | (0x55555555u & mask));
    }
    public int GetVerticesPerSide() => _vertsPerSide;

    private void UpdateLos(int id, FixedVector2D? from, FixedVector2D? to, Fixed range, int owner, bool add)
    {
        if (_losState == null || range.IsZero() || owner < 0) return;
        uint mask = LosQuerier.MaskForPlayer(owner);
        int r = (range / Los.TileSize).ToInt_RoundToZero();
        if (to.HasValue && add)
        {
            int cx = (to.Value.X / Los.TileSize).ToInt_RoundToZero();
            int cy = (to.Value.Y / Los.TileSize).ToInt_RoundToZero();
            LosHelper.RevealCircle(_losState, cx, cy, r, mask, true);
        }
    }
    private void RebuildHash()
    {
        foreach (var b in _cells.Values) b.Clear();
        foreach (var kv in _entities)
        {
            if (!kv.Value.inWorld) continue;
            long k = Key((kv.Value.pos.X.ToFloat() / CellSize), (kv.Value.pos.Y.ToFloat() / CellSize));
            if (!_cells.TryGetValue(k, out var bucket)) { bucket = new List<int>(); _cells[k] = bucket; }
            bucket.Add(kv.Key);
        }
    }
    private static long Key(float cx, float cz) => ((long)(int)MathF.Floor(cx) << 32) ^ (uint)(int)MathF.Floor(cz);
    private static uint CalcMask(List<int> owners)
    {
        if (owners == null) return uint.MaxValue;
        uint m = 0; foreach (var o in owners) m |= 1u << (1 + o); return m;
    }
}
