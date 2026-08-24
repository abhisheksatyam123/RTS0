/* Ported from source/simulation2/components/CCmpPathfinder.cpp + ICmpPathfinder.h — 0 A.D. GPL-2.0 */
/// <summary>Pathfinder system — wraps Hierarchical/Long/Vertex pathfinders.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class Pathfinder : ISystemComponent
{
    public int EntityId => 0;
    public void Init(Dictionary<string, object> p) { }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { }
    public void Deserialize(System.IO.BinaryReader r) { }

    private Grid<byte> _grid;
    private Grid<byte> _terrainOnly;
    private HierarchicalPathfinder _hier;
    private LongPathfinder _long;
    private VertexPathfinder _vertex;
    private readonly Dictionary<string, ushort> _passMasks = new();
    private readonly Dictionary<ushort, Fixed> _clearances = new();
    private uint _nextTicket = 1;

    public Pathfinder() { }
    public Pathfinder(Grid<byte> grid, HierarchicalPathfinder hier, LongPathfinder lp) : this(grid, hier, lp, null) { }
    public Pathfinder(HierarchicalPathfinder hier, LongPathfinder lp, VertexPathfinder vp, Grid<byte> grid)
    {
        _hier = hier; _long = lp; _vertex = vp; _grid = grid;
        InitPassClasses();
    }
    public Pathfinder(Grid<byte> grid, HierarchicalPathfinder hier, LongPathfinder lp, VertexPathfinder vp)
    {
        _grid = grid; _hier = hier; _long = lp; _vertex = vp;
        InitPassClasses();
    }

    private void InitPassClasses()
    {
        _passMasks["default"] = 1; _clearances[1] = Fixed.FromInt(1);
        _passMasks["default-terrain-only"] = 2; _clearances[2] = Fixed.Zero;
        _passMasks["unrestricted"] = 4; _clearances[4] = Fixed.Zero;
    }

    public void SetGrid(Grid<byte> grid, HierarchicalPathfinder hier)
    {
        _grid = grid; _hier = hier;
        if (grid != null) _long = new LongPathfinder(grid, hier);
        if (_passMasks.Count == 0) InitPassClasses();
        if (grid != null) _vertex = new VertexPathfinder((ushort)grid.Width, new Grid<ushort>((ushort)grid.Width, (ushort)grid.Height));
    }

    public ushort GetPassabilityClass(string name)
    {
        if (_passMasks.TryGetValue(name, out var m)) return m;
        return 1;
    }
    public void GetPassabilityClasses(Dictionary<string, ushort> outDict) { foreach (var kv in _passMasks) outDict[kv.Key] = kv.Value; }
    public Fixed GetClearance(ushort passClass) => _clearances.TryGetValue(passClass, out var c) ? c : Fixed.FromInt(1);
    public Fixed GetMaximumClearance() { Fixed m = Fixed.Zero; foreach (var v in _clearances.Values) if (v > m) m = v; return m; }
    public Grid<byte> GetPassabilityGrid() => _grid;

    public void ComputePath(Fixed x0, Fixed z0, PathGoal goal, ushort passClass, WaypointPath ret) => ComputePathImmediate(x0, z0, goal, passClass, ret);
    // ---- immediate path computation ----
    public void ComputePathImmediate(Fixed x0, Fixed z0, PathGoal goal, ushort passClass, WaypointPath ret)
    {
        if (_long == null || _grid == null) return;
        int sx = (x0 / Pathfinding.NavcellSizeInt).ToInt_RoundToZero();
        int sz = (z0 / Pathfinding.NavcellSizeInt).ToInt_RoundToZero();
        int gx = (goal.X / Pathfinding.NavcellSizeInt).ToInt_RoundToZero();
        int gz = (goal.Z / Pathfinding.NavcellSizeInt).ToInt_RoundToZero();
        var path = _long.FindPath(sx, sz, gx, gz);
        if (path == null) return;
        ret.Clear();
        foreach (var (x, z) in path) ret.Waypoints.Add(new Waypoint(Fixed.FromInt(x), Fixed.FromInt(z)));
    }

    public WaypointPath ComputeShortPathImmediate(ShortPathRequest req)
    {
        var obs = new List<ObstructionSquare>();
        return _vertex != null ? _vertex.ComputeShortPath(req, obs) : new WaypointPath();
    }

    // ---- async stubs (synchronous for determinism) ----
    public uint ComputePathAsync(Fixed x0, Fixed z0, PathGoal goal, ushort passClass, int notify)
    {
        var ret = new WaypointPath(); ComputePathImmediate(x0, z0, goal, passClass, ret);
        return _nextTicket++;
    }
    public uint ComputeShortPathAsync(Fixed x0, Fixed z0, Fixed clearance, Fixed range, PathGoal goal, ushort passClass, bool avoidMoving, int group, int notify)
        => _nextTicket++;

    // ---- queries ----
    public bool IsGoalReachable(Fixed x0, Fixed z0, PathGoal goal, ushort passClass)
    {
        if (_hier == null || _grid == null) return false;
        int sx = (x0 / 1).ToInt_RoundToZero(), sz = (z0 / 1).ToInt_RoundToZero();
        int gx = (goal.X / 1).ToInt_RoundToZero(), gz = (goal.Z / 1).ToInt_RoundToZero();
        return _hier.IsReachable(sx, sz, gx, gz, _grid);
    }

    public bool CheckMovement(Fixed x0, Fixed z0, Fixed x1, Fixed z1, Fixed r, ushort passClass)
    {
        if (_grid == null) return true;
        return Pathfinding.CheckLineMovement(x0, z0, x1, z1, passClass, new Grid<ushort>((ushort)_grid.Width, (ushort)_grid.Height));
    }

    public List<FixedVector2D> DistributeAround(List<int> units, Fixed x, Fixed z)
    {
        var ret = new List<FixedVector2D>();
        float angle = 0;
        for (int i = 0; i < units.Count; i++)
        {
            float rad = 2f + i * 0.7f;
            angle += 1.2f;
            ret.Add(new FixedVector2D(Fixed.FromInt((int)(x.ToFloat() + MathF.Cos(angle) * rad)), Fixed.FromInt((int)(z.ToFloat() + MathF.Sin(angle) * rad))));
        }
        return ret;
    }

    public void UpdateGrid() => _hier?.Recompute(_grid);
    public void SetDebugOverlay(bool v) { }
    public void SetDebugPath(Fixed x0, Fixed z0, PathGoal goal, ushort passClass) { }
}
