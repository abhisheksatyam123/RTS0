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

    private int _nextId = 1;
    private readonly HashSet<Vector2Int> _occupiedCells = new();
    private float _goldCarry;

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
        PlaceBuildingExact(new Vector2Int(0, 0), isCenter: true);
        for (int i = 0; i < 8; i++)
        {
            float angle = Mathf.Tau * i / 8.0f;
            SpawnUnit(new Vector3(Mathf.Cos(angle) * 5.0f, 0.0f, Mathf.Sin(angle) * 5.0f + 7.0f));
        }
    }

    public SimUnit SpawnUnit(Vector3 pos)
    {
        var unit = new SimUnit { Id = _nextId++, Pos = pos, PrevPos = pos };
        Units.Add(unit);
        UnitById[unit.Id] = unit;
        return unit;
    }

    public void Apply(GameCommand command)
    {
        switch (command)
        {
            case MoveCommand move: ApplyMove(move); break;
            case BuildHouseCommand build: ApplyBuild(build); break;
        }
    }

    public void Tick(float dt)
    {
        foreach (SimUnit u in Units) u.PrevPos = u.Pos;

        MovePass(dt);
        SeparationPass();
        ClampToMap();

        // Placeholder economy: each worker produces 1 gold/sec.
        _goldCarry += dt * Units.Count;
        while (_goldCarry >= 1.0f)
        {
            _goldCarry -= 1.0f;
            Gold++;
        }
    }

    public bool CanPlace(Vector2Int anchor, bool isCenter)
    {
        int footprint = isCenter ? CenterFootprint : HouseFootprint;
        int limit = (int)(MapHalfExtent / Grid) - footprint;

        for (int x = 0; x < footprint; x++)
        for (int y = 0; y < footprint; y++)
        {
            var cell = anchor + new Vector2Int(x, y);
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
            Vector3 offset = FormationOffsets[i % FormationOffsets.Length];
            unit.Target = cmd.Target + offset;
            unit.HasOrder = true;
            i++;
        }
    }

    private void ApplyBuild(BuildHouseCommand cmd)
    {
        if (Gold < HouseCost || !CanPlace(cmd.Cell, isCenter: false)) return;
        Gold -= HouseCost;
        PlaceBuildingExact(cmd.Cell, isCenter: false);
    }

    private void PlaceBuildingExact(Vector2Int anchor, bool isCenter)
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
            _occupiedCells.Add(anchor + new Vector2Int(x, y));
    }

    private void MovePass(float dt)
    {
        foreach (SimUnit u in Units)
        {
            if (!u.HasOrder) continue;
            Vector3 to = u.Target - u.Pos;
            to.Y = 0.0f;
            float dist = to.Length();
            if (dist < ArriveDist)
            {
                u.HasOrder = false;
                continue;
            }
            u.Pos += to / dist * UnitSpeed * dt;
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
