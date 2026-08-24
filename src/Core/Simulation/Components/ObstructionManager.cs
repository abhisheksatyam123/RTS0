/* Ported from source/simulation2/components/CCmpObstructionManager.cpp + ICmpObstructionManager.h — 0 A.D. GPL-2.0 */
/// <summary>Manages obstructions for pathfinding — mirrors CCmpObstructionManager.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class ObstructionManager : ISystemComponent
{
    public int EntityId => 0;
    public void Init(Dictionary<string, object> p) { }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { }
    public void Deserialize(System.IO.BinaryReader r) { }

    private struct UnitShape { public int entity; public FixedVector2D pos; public Fixed clearance; public byte flags; public int group; }
    private struct StaticShape { public int entity; public FixedVector2D pos; public FixedVector2D u, v; public Fixed hw, hh; public byte flags; public int group, group2; }

    private readonly Dictionary<uint, UnitShape> _units = new();
    private readonly Dictionary<uint, StaticShape> _statics = new();
    private uint _nextUnit = 1, _nextStatic = 1;
    private Grid<byte> _dirtGrid;
    private bool _dirty, _globalDirty;
    private Fixed _worldX0, _worldZ0, _worldX1, _worldZ1;
    private Fixed _maxClearance = Fixed.FromInt(2);
    private bool _passCircular;

    public const byte FlagBlockMovement = 1 << 0, FlagBlockPathfinding = 1 << 3, FlagMoving = 1 << 4;

    public struct Tag { public uint n; public bool Valid => n != 0; public Tag(uint v) => n = v; }
    private static bool IsUnit(Tag t) => (t.n & 1) == 0;
    private static uint ToIndex(Tag t) => t.n >> 1;

    public void SetBounds(Fixed x0, Fixed z0, Fixed x1, Fixed z1)
    {
        _worldX0 = x0; _worldZ0 = z0; _worldX1 = x1; _worldZ1 = z1;
        int sz = ((x1 - x0) / Pathfinding.NavcellSizeInt).ToInt_RoundToZero();
        _dirtGrid = new Grid<byte>((ushort)sz, (ushort)sz);
        _globalDirty = _dirty = true;
    }

    public Tag AddUnitShape(int ent, Fixed x, Fixed z, Fixed clearance) => AddUnitShape(ent, x, z, clearance, FlagBlockPathfinding, 0);
    public Tag AddUnitShape(int ent, Fixed x, Fixed z, Fixed clearance, byte flags, int group)
    {
        uint id = _nextUnit++; _units[id] = new UnitShape { entity = ent, pos = new FixedVector2D(x, z), clearance = clearance, flags = flags, group = group };
        MarkDirty(x, z, clearance); return new Tag((id << 1) | 0);
    }
    public void MoveShape(int entity, Fixed x, Fixed z)
    {
        foreach (var kv in _units) if (kv.Value.entity == entity) { MoveShape(new Tag((kv.Key << 1) | 0), x, z, Fixed.Zero); return; }
    }
    public void UpdateGrid(Grid<byte> grid, int offset)
    {
        var tmp = new Grid<ushort>((ushort)grid.Width, (ushort)grid.Height);
        Rasterize(tmp, null, true);
        for (int y = 0; y < grid.Height; y++) for (int x = 0; x < grid.Width; x++) if (tmp.Get(x, y) != 0) grid.Set(x, y, 0);
    }
    public Tag AddStaticShape(int ent, Fixed x, Fixed z, Fixed angle, Fixed w, Fixed h, byte flags, int group, int group2 = -1)
    {
        // angle approx: use axis-aligned for now
        var u = new FixedVector2D(Fixed.One, Fixed.Zero); var v = new FixedVector2D(Fixed.Zero, Fixed.One);
        uint id = _nextStatic++; _statics[id] = new StaticShape { entity = ent, pos = new FixedVector2D(x, z), u = u, v = v, hw = w / 2, hh = h / 2, flags = flags, group = group, group2 = group2 };
        MarkDirty(x, z, w > h ? w / 2 : h / 2); return new Tag((id << 1) | 1);
    }
    public void MoveShape(Tag tag, Fixed x, Fixed z, Fixed angle)
    {
        if (!tag.Valid) return;
        if (IsUnit(tag))
        {
            if (!_units.TryGetValue(ToIndex(tag), out var s)) return;
            MarkDirty(s.pos.X, s.pos.Y, s.clearance); s.pos = new FixedVector2D(x, z); _units[ToIndex(tag)] = s; MarkDirty(x, z, s.clearance);
        }
        else
        {
            if (!_statics.TryGetValue(ToIndex(tag), out var s)) return;
            MarkDirty(s.pos.X, s.pos.Y, s.hw); s.pos = new FixedVector2D(x, z); _statics[ToIndex(tag)] = s; MarkDirty(x, z, s.hw);
        }
    }
    public void RemoveShape(Tag tag)
    {
        if (!tag.Valid) return;
        if (IsUnit(tag)) { if (_units.TryGetValue(ToIndex(tag), out var s)) MarkDirty(s.pos.X, s.pos.Y, s.clearance); _units.Remove(ToIndex(tag)); }
        else { if (_statics.TryGetValue(ToIndex(tag), out var s)) MarkDirty(s.pos.X, s.pos.Y, s.hw); _statics.Remove(ToIndex(tag)); }
    }
    public ObstructionSquare GetObstruction(Tag tag)
    {
        if (IsUnit(tag)) { var s = _units[ToIndex(tag)]; return new ObstructionSquare(s.pos.X, s.pos.Y, new FixedVector2D(Fixed.One, Fixed.Zero), new FixedVector2D(Fixed.Zero, Fixed.One), s.clearance, s.clearance); }
        var st = _statics[ToIndex(tag)]; return new ObstructionSquare(st.pos.X, st.pos.Y, st.u, st.v, st.hw, st.hh);
    }

    public bool TestLine(Fixed x0, Fixed z0, Fixed x1, Fixed z1, Fixed r)
    {
        foreach (var s in _units.Values)
        {
            FixedVector2D c = s.pos;
            if (Geometry.TestRayAASquare(new FixedVector2D(x0, z0) - c, new FixedVector2D(x1, z1) - c, new FixedVector2D(s.clearance + r, s.clearance + r))) return true;
        }
        foreach (var s in _statics.Values)
        {
            FixedVector2D c = s.pos;
            if (Geometry.TestRaySquare(new FixedVector2D(x0, z0) - c, new FixedVector2D(x1, z1) - c, s.u, s.v, new FixedVector2D(s.hw + r, s.hh + r))) return true;
        }
        return false;
    }
    public bool TestUnitShape(Fixed x, Fixed z, Fixed clearance)
    {
        var pos = new FixedVector2D(x, z);
        foreach (var s in _units.Values) if ((s.pos - pos).Length() < s.clearance + clearance) return true;
        foreach (var s in _statics.Values) if (Geometry.DistanceToSquare(pos - s.pos, s.u, s.v, new FixedVector2D(s.hw, s.hh)) < clearance) return true;
        return false;
    }

    public void Rasterize(Grid<ushort> grid, List<PathfinderPassability> passes, bool fullUpdate)
    {
        foreach (var st in _statics.Values)
        {
            if ((st.flags & FlagBlockPathfinding) == 0) continue;
            var sq = new ObstructionSquare(st.pos.X, st.pos.Y, st.u, st.v, st.hw, st.hh);
            var spans = new List<Span>(); global::Rts0.Core.Rasterize.RasterizeRectWithClearance(spans, sq, Fixed.Zero, Pathfinding.NavcellSize);
            foreach (var sp in spans) for (int i = sp.I0; i < sp.I1; i++) if (i >= 0 && sp.J >= 0 && i < grid.Width && sp.J < grid.Height) grid.Set(i, sp.J, (ushort)(grid.Get(i, sp.J) | 1));
        }
        foreach (var u in _units.Values)
        {
            if ((u.flags & FlagBlockPathfinding) == 0) continue;
            ushort i0, j0, i1, j1; Pathfinding.NearestNavcell(u.pos.X - u.clearance, u.pos.Y - u.clearance, out i0, out j0, grid.Width, grid.Height);
            Pathfinding.NearestNavcell(u.pos.X + u.clearance, u.pos.Y + u.clearance, out i1, out j1, grid.Width, grid.Height);
            for (int j = j0 + 1; j < j1; j++) for (int i = i0 + 1; i < i1; i++) grid.Set(i, j, (ushort)(grid.Get(i, j) | 1));
        }
        _dirty = _globalDirty = false;
    }

    public void GetObstructionsInRange(Fixed x0, Fixed z0, Fixed x1, Fixed z1, List<ObstructionSquare> outSquares)
    {
        foreach (var s in _units.Values)
        {
            if (s.pos.X < x0 || s.pos.X > x1 || s.pos.Y < z0 || s.pos.Y > z1) continue;
            outSquares.Add(new ObstructionSquare(s.pos.X, s.pos.Y, new FixedVector2D(Fixed.One, Fixed.Zero), new FixedVector2D(Fixed.Zero, Fixed.One), s.clearance, s.clearance));
        }
        foreach (var s in _statics.Values)
        {
            if (s.pos.X < x0 || s.pos.X > x1 || s.pos.Y < z0 || s.pos.Y > z1) continue;
            outSquares.Add(new ObstructionSquare(s.pos.X, s.pos.Y, s.u, s.v, s.hw, s.hh));
        }
    }

    public void SetPassabilityCircular(bool v) { _passCircular = v; _globalDirty = _dirty = true; }
    public bool GetPassabilityCircular() => _passCircular;
    public void SetDebugOverlay(bool v) { }

    private void MarkDirty(Fixed x, Fixed z, Fixed r)
    {
        if (_dirtGrid == null) return;
        _dirty = true;
        ushort i0, j0, i1, j1;
        Pathfinding.NearestNavcell(x - r - _maxClearance, z - r - _maxClearance, out i0, out j0, _dirtGrid.Width, _dirtGrid.Height);
        Pathfinding.NearestNavcell(x + r + _maxClearance, z + r + _maxClearance, out i1, out j1, _dirtGrid.Width, _dirtGrid.Height);
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) _dirtGrid.Set(i, j, 1);
    }
}
