using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Godot;

namespace Rts0.Core;

public class GameSim
{
    public const float Grid = 2.0f;
    public const int HouseCost = 50;
    public const int StartGold = 150;
    public const float MapHalfExtent = 30.0f;
    public const float FixedStep = 1.0f / 20.0f;

    private const float UnitSpeed = 6.0f;
    private const float ArriveDist = 0.35f;
    private const float UnitRadius = 0.45f;
    private const float CellSize = UnitRadius * 2.0f;   // spatial hash cell
    private const int CenterFootprint = 2;
    private const int HouseFootprint = 1;
    private const int ParallelMinUnits = 256;           // below this, threading overhead wins

    public int Gold = StartGold;
    public readonly List<SimUnit> Units = new();
    public readonly Dictionary<int, SimUnit> UnitById = new();
    public readonly List<SimBuilding> Buildings = new();
    public readonly List<SimResource> Resources = new();
    public readonly Dictionary<int, SimResource> ResourceById = new();

    private int _nextId = 1;
    private readonly HashSet<Vector2I> _occupiedCells = new();
    private float _goldCarry;
    private readonly Dictionary<int, UnitAI> _aiByUnit = new();

    // 0 A.D.-derived deterministic pathfinding (Fixed/Grid/Hierarchical/Long)
    private Grid<byte> _passGrid;
    private HierarchicalPathfinder _hier;
    private LongPathfinder _long;
    private VertexPathfinder _vertex;
    private Pathfinder _pathfinderSystem;
    private RangeManager _rangeManager;
    private ObstructionManager _obstructionManager;
    private TerritoryManager _territoryManager;
    private readonly Dictionary<int, PlayerComponent> _players = new();

    // Spatial hash + force buffers (grown on demand, reused across ticks: zero steady-state allocs)
    private readonly Dictionary<long, List<int>> _cells = new();
    private float[] _forceX = Array.Empty<float>();
    private float[] _forceZ = Array.Empty<float>();

    private static readonly Vector3[] FormationOffsets =
    {
        Vector3.Zero,
        new(1.6f, 0, 0), new(-1.6f, 0, 0),
        new(0, 0, 1.6f), new(0, 0, -1.6f),
        new(2.2f, 0, 2.2f), new(-2.2f, 0, 2.2f),
        new(2.2f, 0, -2.2f), new(-2.2f, 0, -2.2f),
    };

    public void SpawnInitial()
    {
        // 0 A.D. managers — mirror source/simulation2/system/ComponentManager init order
        _rangeManager = new RangeManager();
        _rangeManager.SetBounds(Fixed.FromInt((int)-MapHalfExtent), Fixed.FromInt((int)-MapHalfExtent), Fixed.FromInt((int)MapHalfExtent), Fixed.FromInt((int)MapHalfExtent));
        _obstructionManager = new ObstructionManager();
        _territoryManager = new TerritoryManager();
        _territoryManager.Init((int)(MapHalfExtent * 2 / Grid), (int)(MapHalfExtent * 2 / Grid));
        for (int p = 0; p < 2; p++) { var pc = new PlayerComponent(); pc.Init(p); _players[p] = pc; }

        RebuildPassability();
        PlaceBuildingExact(new Vector2I(0, 0), isCenter: true);
        for (int i = 0; i < 8; i++)
        {
            float angle = Mathf.Tau * i / 8.0f;
            SpawnUnit(new Vector3(Mathf.Cos(angle) * 5.0f, 0.0f, Mathf.Sin(angle) * 5.0f + 7.0f));
        }
        // Thronefall-style scattered resources (0 A.D. ResourceSupply pattern: small patches near start)
        SpawnResource(new Vector2I(5, 4), ResourceType.Tree);
        SpawnResource(new Vector2I(-6, 5), ResourceType.Tree);
        SpawnResource(new Vector2I(4, -6), ResourceType.GoldVein);
        SpawnResource(new Vector2I(-5, -5), ResourceType.GoldVein);
        RebuildPassability();
        // Initial territory & LOS reveal (mirrors CCmpRangeManager + CCmpTerritoryManager)
        _rangeManager.SetLosRevealAll(0, true);
        _territoryManager.AddInfluence(new Vector2I(0, 0), 1, 2);
        _territoryManager.CalculateTerritories();
    }

    public SimUnit SpawnUnit(Vector3 pos)
    {
        var unit = new SimUnit { Id = _nextId++, Pos = pos, PrevPos = pos, Hp = 100, MaxHp = 100 };
        // Wire 0 A.D. components — mirrors CCmpPosition / CCmpOwnership / CCmpVision / CCmpUnitMotion
        unit.PositionComp = new Position(unit.Id);
        unit.PositionComp.SetPosition(Fixed.FromFloat(pos.X), Fixed.FromFloat(pos.Z));
        unit.OwnershipComp = new Ownership(unit.Id, (int)unit.Team);
        unit.VisionComp = new Vision(unit.Id, Fixed.FromInt(24));
        unit.MotionComp = new UnitMotion(unit.Id);
        Units.Add(unit);
        UnitById[unit.Id] = unit;
        _aiByUnit[unit.Id] = new UnitAI(unit);
        _rangeManager?.AddEntity(unit.Id, unit.PositionComp.GetPosition2D(), (int)unit.Team, Fixed.FromFloat(UnitRadius), Fixed.FromInt(24));
        _obstructionManager?.AddUnitShape(unit.Id, Fixed.FromFloat(pos.X), Fixed.FromFloat(pos.Z), Fixed.FromFloat(UnitRadius));
        return unit;
    }

    public SimResource SpawnResource(Vector2I cell, ResourceType type)
    {
        var res = new SimResource { Id = _nextId++, Cell = cell, Type = type, Amount = type == ResourceType.Tree ? 300 : 500 };
        Resources.Add(res);
        ResourceById[res.Id] = res;
        return res;
    }

    public void Apply(GameCommand command)
    {
        switch (command)
        {
            case MoveCommand move: ApplyMove(move); break;
            case BuildHouseCommand build: ApplyBuild(build); break;
            case GatherCommand gather: ApplyGather(gather); break;
            case AttackCommand attack: ApplyAttack(attack); break;
        }
    }

    public SimResource GetResource(int id) => ResourceById.TryGetValue(id, out var r) ? r : null;

    public void AssignPath(SimUnit unit, Vector3 target)
    {
        if (_passGrid == null) { unit.Target = target; unit.HasOrder = true; return; }
        var startCell = WorldToGrid(WorldToCell(unit.Pos));
        var goalCell = WorldToGrid(WorldToCell(target));
        var path = _long?.FindPath(startCell.X, startCell.Y, goalCell.X, goalCell.Y);
        if (path != null && path.Count > 1)
        {
            unit.Path = new List<Vector3>(path.Count);
            foreach (var (gx, gy) in path) unit.Path.Add(GridToWorld(gx, gy));
            unit.PathIndex = 1;
            unit.Target = unit.Path[unit.PathIndex];
            unit.HasOrder = true;
        }
        else
        {
            unit.Path = null;
            unit.Target = target;
            unit.HasOrder = true;
        }
    }

    private static Vector2I WorldToCell(Vector3 pos) => new(Mathf.FloorToInt(pos.X / Grid), Mathf.FloorToInt(pos.Z / Grid));
    private static Vector2I WorldToGrid(Vector2I cell)
    {
        int offset = (int)(MapHalfExtent / Grid); // 15
        return new(cell.X + offset, cell.Y + offset);
    }
    private static Vector3 GridToWorld(int gx, int gy)
    {
        int offset = (int)(MapHalfExtent / Grid);
        return new((gx - offset + 0.5f) * Grid, 0, (gy - offset + 0.5f) * Grid);
    }
    private static Vector3 CellToWorld(int x, int y) => GridToWorld(x, y);

    public void Tick(float dt)
    {
        foreach (SimUnit u in Units) u.PrevPos = u.Pos;

        // 0 A.D.-style: tick UnitAI FSMs first (orders -> paths), then movement.
        foreach (var u in Units)
            if (_aiByUnit.TryGetValue(u.Id, out var ai)) ai.Update(this);

        // 0 A.D. managers tick — mirrors CCmpRangeManager / CCmpTerritoryManager / CCmpObstructionManager
        foreach (var u in Units)
        {
            if (u.PositionComp != null)
            {
                u.PositionComp.SetPosition(Fixed.FromFloat(u.Pos.X), Fixed.FromFloat(u.Pos.Z));
                _rangeManager?.MoveEntity(u.Id, u.PositionComp.GetPosition2D());
                _obstructionManager?.MoveShape(u.Id, Fixed.FromFloat(u.Pos.X), Fixed.FromFloat(u.Pos.Z));
            }
            u.MotionComp?.Tick(u.PositionComp, _pathfinderSystem, Fixed.FromFloat(dt));
        }
        _territoryManager?.Tick(dt);

        MovePass(dt);
        SeparationPass();
        ClampToMap();
        TickGather(dt);
        TickCombat(dt);
        TickWaves(dt);
        // ResourceTrickle / Population tick — mirrors ResourceTrickle.js / Population.js
        foreach (var kv in _players) kv.Value?.Tick(dt, (type, amt) => Gold += amt);

        // Fallback trickle if no resources gathered (keeps early game from stalling)
        _goldCarry += dt * 0.2f * Units.Count;
        while (_goldCarry >= 1.0f)
        {
            _goldCarry -= 1.0f;
            Gold++;
        }
    }

    public bool CanPlace(Vector2I anchor, bool isCenter)
    {
        int footprint = isCenter ? CenterFootprint : HouseFootprint;
        int limit = (int)(MapHalfExtent / Grid) - footprint;

        for (int x = 0; x < footprint; x++)
        for (int y = 0; y < footprint; y++)
        {
            var cell = anchor + new Vector2I(x, y);
            if (Mathf.Abs(cell.X) > limit || Mathf.Abs(cell.Y) > limit) return false;
            if (_occupiedCells.Contains(cell)) return false;
        }
        return true;
    }

    private void ApplyMove(MoveCommand cmd)
    {
        int i = 0;
        foreach (int id in cmd.UnitIds)
        {
            if (!UnitById.TryGetValue(id, out SimUnit unit)) continue;
            if (!_aiByUnit.TryGetValue(id, out var ai)) continue;
            Vector3 offset = FormationOffsets[i % FormationOffsets.Length];
            // Clear incompatible gathering state, push walk order (mirrors UnitAI.js Order.Walk)
            ai.PushOrder(new MoveCommand { PlayerId = cmd.PlayerId, UnitIds = new[] { id }, Target = cmd.Target + offset });
            i++;
        }
    }

    private void ApplyBuild(BuildHouseCommand cmd)
    {
        if (Gold < HouseCost || !CanPlace(cmd.Cell, isCenter: false)) return;
        Gold -= HouseCost;
        PlaceBuildingExact(cmd.Cell, isCenter: false);
        RebuildPassability();
    }

    private void ApplyGather(GatherCommand cmd)
    {
        if (!ResourceById.TryGetValue(cmd.ResourceId, out _)) return;
        // Routed per-unit via OrderGather — this generic entry stays for network replay.
    }

    private void ApplyAttack(AttackCommand cmd)
    {
        // Routed per-unit via OrderAttack
    }

    public void OrderGather(int[] unitIds, int resourceId)
    {
        if (!ResourceById.ContainsKey(resourceId)) return;
        foreach (int id in unitIds)
        {
            if (!UnitById.TryGetValue(id, out var u) || u.Team != Team.Player) continue;
            if (_aiByUnit.TryGetValue(id, out var ai))
                ai.PushOrder(new GatherCommand { PlayerId = 0, ResourceId = resourceId });
        }
    }

    public void OrderAttack(int[] unitIds, int targetId)
    {
        if (!UnitById.TryGetValue(targetId, out var target) || target.Hp <= 0) return;
        foreach (int id in unitIds)
        {
            if (!UnitById.TryGetValue(id, out var u) || u.Team != Team.Player) continue;
            if (_aiByUnit.TryGetValue(id, out var ai))
                ai.PushOrder(new AttackCommand { PlayerId = 0, TargetId = targetId });
        }
    }

    private void PlaceBuildingExact(Vector2I anchor, bool isCenter)
    {
        int footprint = isCenter ? CenterFootprint : HouseFootprint;
        var building = new SimBuilding
        {
            Id = _nextId++,
            AnchorCell = anchor,
            Footprint = footprint,
            SizeMeters = isCenter ? 4.0f : 1.8f,
            IsCenter = isCenter,
        };
        Buildings.Add(building);
        for (int x = 0; x < footprint; x++)
        for (int y = 0; y < footprint; y++)
            _occupiedCells.Add(anchor + new Vector2I(x, y));
    }

    private void RebuildPassability()
    {
        int cells = (int)(MapHalfExtent * 2 / Grid);
        _passGrid = new Grid<byte>((ushort)cells, (ushort)cells);
        for (int y = 0; y < cells; y++)
        for (int x = 0; x < cells; x++)
            _passGrid.Set(x, y, 1);
        int offset = cells / 2;
        foreach (var c in _occupiedCells)
        {
            int gx = c.X + offset;
            int gy = c.Y + offset;
            if (gx >= 0 && gy >= 0 && gx < cells && gy < cells)
                _passGrid.Set(gx, gy, 0);
        }
        _hier = new HierarchicalPathfinder(cells, cells);
        _hier.Recompute(_passGrid);
        _long = new LongPathfinder(_passGrid, _hier);
        // Vertex short pathfinder + system wrapper — mirrors CCmpPathfinder
        _vertex = new VertexPathfinder((ushort)cells, new Grid<ushort>((ushort)cells, (ushort)cells));
        _pathfinderSystem = new Pathfinder(_hier, _long, _vertex, _passGrid);
        // rasterize obstructions for next tick (mirrors CCmpObstructionManager.Rasterize)
        _obstructionManager?.UpdateGrid(_passGrid, offset);
    }

    private float _gatherCarry;
    private void TickGather(float dt)
    {
        // Mirrors 0 A.D. ResourceGatherer.js: gather 10 per second when in range, return to dropsite
        var center = Buildings.Find(b => b.IsCenter);
        if (center == null) return;
        foreach (var u in Units)
        {
            if (u.Team != Team.Player || u.Hp <= 0) continue;
            if (u.Carry >= u.CarryCapacity)
            {
                if (u.Pos.DistanceTo(center.CenterPos) < 2.5f)
                {
                    Gold += u.Carry;
                    u.Carry = 0;
                }
                else if (!u.HasOrder)
                {
                    AssignPath(u, center.CenterPos);
                }
                continue;
            }
            // Find nearest resource with amount
            SimResource best = null; float bestDist = float.MaxValue;
            foreach (var r in Resources)
            {
                if (r.Amount <= 0) continue;
                float d = u.Pos.DistanceTo(r.Pos);
                if (d < bestDist) { bestDist = d; best = r; }
            }
            if (best == null) continue;
            if (bestDist < 1.8f)
            {
                u.AttackTimer += dt;
                if (u.AttackTimer >= 0.8f)
                {
                    u.AttackTimer = 0;
                    int take = Math.Min(10, Math.Min(best.Amount, u.CarryCapacity - u.Carry));
                    best.Amount -= take;
                    u.Carry += take;
                }
            }
            else if (!u.HasOrder)
            {
                AssignPath(u, best.Pos);
            }
        }
        // Remove depleted resources (visual cleanup handled by WorldView)
        Resources.RemoveAll(r => r.Amount <= 0);
        foreach (var id in new List<int>(ResourceById.Keys))
            if (ResourceById[id].Amount <= 0) ResourceById.Remove(id);
    }

    // ---- Combat — port of Attack.js / Health.js ----
    private void TickCombat(float dt)
    {
        foreach (var u in Units) u.AttackTimer -= dt;
        // Simple O(n^2) scan is fine for <200 units; will move to spatial query when scaling to 1k+
        var toRemove = new List<SimUnit>();
        foreach (var attacker in Units)
        {
            if (attacker.Hp <= 0 || attacker.AttackTimer > 0) continue;
            SimUnit target = null; float best = attacker.AttackRange + 0.5f;
            foreach (var v in Units)
            {
                if (v.Team == attacker.Team || v.Hp <= 0) continue;
                float d = attacker.Pos.DistanceTo(v.Pos);
                if (d < best) { best = d; target = v; }
            }
            if (target != null)
            {
                target.Hp -= attacker.AttackDamage;
                attacker.AttackTimer = attacker.AttackCooldown;
                if (target.Hp <= 0) toRemove.Add(target);
            }
        }
        foreach (var dead in toRemove)
        {
            Units.Remove(dead);
            UnitById.Remove(dead.Id);
            _aiByUnit.Remove(dead.Id);
        }
    }

    private float _waveTimer;
    private int _waveNumber;
    public int WaveNumber => _waveNumber;
    private void TickWaves(float dt)
    {
        _waveTimer += dt;
        if (_waveTimer < 25f) return;
        _waveTimer = 0; _waveNumber++;
        // Spawn at random map edge — mirrors 0 A.D. Trigger.js wave spawns
        var rnd = new Random(_waveNumber * 9973);
        int count = 3 + _waveNumber * 2;
        for (int i = 0; i < count; i++)
        {
            float angle = (float)rnd.NextDouble() * Mathf.Tau;
            float edge = MapHalfExtent - 1;
            var pos = new Vector3(Mathf.Cos(angle) * edge, 0, Mathf.Sin(angle) * edge);
            var e = SpawnUnit(pos);
            e.Team = Team.Enemy;
            e.Hp = 60 + _waveNumber * 10;
            e.MaxHp = e.Hp;
            // Push initial attack order toward town center so UnitAI takes over
            if (_aiByUnit.TryGetValue(e.Id, out var ai))
                ai.PushOrder(new AttackCommand { PlayerId = 1, TargetId = Units.Find(u => u.Team == Team.Player)?.Id ?? e.Id });
        }
    }

    private void MovePass(float dt)
    {
        foreach (SimUnit u in Units)
        {
            if (!u.HasOrder) continue;
            // If we have a computed path, follow waypoints (0 A.D. LongPathfinder style)
            if (u.Path != null && u.PathIndex < u.Path.Count)
            {
                Vector3 wp = u.Path[u.PathIndex];
                Vector3 to = wp - u.Pos; to.Y = 0;
                float dist = to.Length();
                if (dist < 0.6f)
                {
                    u.PathIndex++;
                    if (u.PathIndex >= u.Path.Count)
                    {
                        u.Path = null;
                        // Snap target to final waypoint; check arrival at actual order target
                        Vector3 toFinal = u.Target - u.Pos; toFinal.Y = 0;
                        if (toFinal.Length() < ArriveDist) u.HasOrder = false;
                    }
                    continue;
                }
                u.Pos += to / dist * UnitSpeed * dt;
                continue;
            }
            Vector3 to2 = u.Target - u.Pos;
            to2.Y = 0.0f;
            float dist2 = to2.Length();
            if (dist2 < ArriveDist)
            {
                u.HasOrder = false;
                continue;
            }
            u.Pos += to2 / dist2 * UnitSpeed * dt;
        }
    }

    private void SeparationPass()
    {
        int count = Units.Count;
        if (count == 0) return;
        if (_forceX.Length < count)
        {
            int cap = Math.Max(count, _forceX.Length * 2);
            Array.Resize(ref _forceX, cap);
            Array.Resize(ref _forceZ, cap);
        }

        BuildSpatialHash();

        // Phase 1 (parallel, read-only over positions): accumulate push forces.
        // Writing to distinct indices per iteration -> no races, and because forces
        // never depend on concurrent writes the result is identical serial or parallel.
        float minDist = UnitRadius * 2.0f;
        if (count >= ParallelMinUnits)
        {
            Parallel.For(0, count, i => AccumulatePush(i, minDist));
        }
        else
        {
            for (int i = 0; i < count; i++) AccumulatePush(i, minDist);
        }

        // Phase 2 (serial apply): deterministic order.
        for (int i = 0; i < count; i++)
        {
            Units[i].Pos += new Vector3(_forceX[i], 0.0f, _forceZ[i]);
        }
    }

    private void AccumulatePush(int i, float minDist)
    {
        SimUnit a = Units[i];
        int cx = (int)MathF.Floor(a.Pos.X / CellSize);
        int cz = (int)MathF.Floor(a.Pos.Z / CellSize);

        float fx = 0.0f, fz = 0.0f;
        for (int ox = -1; ox <= 1; ox++)
        for (int oz = -1; oz <= 1; oz++)
        {
            if (!_cells.TryGetValue(Key(cx + ox, cz + oz), out List<int> bucket)) continue;
            foreach (int j in bucket)
            {
                if (j == i) continue;
                Vector3 d = a.Pos - Units[j].Pos;
                d.Y = 0.0f;
                float dist = d.Length();
                if (dist <= 0.0001f || dist >= minDist) continue;
                float strength = (minDist - dist) * 0.25f;
                fx += d.X / dist * strength;
                fz += d.Z / dist * strength;
            }
        }
        _forceX[i] = fx;
        _forceZ[i] = fz;
    }

    private void BuildSpatialHash()
    {
        foreach (List<int> bucket in _cells.Values) bucket.Clear();
        for (int i = 0; i < Units.Count; i++)
        {
            Vector3 p = Units[i].Pos;
            long key = Key((int)MathF.Floor(p.X / CellSize), (int)MathF.Floor(p.Z / CellSize));
            if (!_cells.TryGetValue(key, out List<int> bucket))
            {
                bucket = new List<int>();
                _cells[key] = bucket;
            }
            bucket.Add(i);
        }
    }

    private static long Key(int cx, int cz) => ((long)cx << 32) ^ (uint)cz;

    private void ClampToMap()
    {
        foreach (SimUnit u in Units)
        {
            u.Pos = new Vector3(
                Mathf.Clamp(u.Pos.X, -MapHalfExtent, MapHalfExtent), 0.0f,
                Mathf.Clamp(u.Pos.Z, -MapHalfExtent, MapHalfExtent));
        }
    }
}
