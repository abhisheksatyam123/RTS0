/* Ported from source/simulation2/system/SimContext.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>Simulation context — mirrors CSimContext (source/simulation2/system/SimContext.h/.cpp).</summary>
/// <remarks>Reference: SimContext.h, SimContext.cpp — holds ComponentManager, Terrain, UnitManager</remarks>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Stub terrain — mirrors CTerrain (only the handle; real terrain lives in GameSim/Grid).</summary>
public sealed class SimTerrain
{
    public int MapSize { get; set; } = 64;
    public float HeightScale { get; set; } = 1f;
}

/// <summary>Stub unit manager — mirrors CUnitManager (lifetime/iteration helper).</summary>
public sealed class SimUnitManager
{
    private readonly HashSet<uint> _alive = new();
    public void Add(uint id) => _alive.Add(id);
    public void Remove(uint id) => _alive.Remove(id);
    public bool Exists(uint id) => _alive.Contains(id);
}

/// <summary>Global simulation context. Mirrors CSimContext.</summary>
public sealed class SimContext
{
    private ComponentManager? _componentManager;
    private readonly SimTerrain? _terrain;
    private readonly SimUnitManager? _unitManager;
    private EntityHandle _systemEntity;

    public SimContext(SimTerrain? terrain = null, SimUnitManager? unitManager = null)
    {
        _terrain = terrain;
        _unitManager = unitManager;
    }

    public ComponentManager GetComponentManager() => _componentManager ?? throw new InvalidOperationException("No ComponentManager");
    public void SetComponentManager(ComponentManager man) => _componentManager = man;

    public bool HasUnitManager() => _unitManager != null;
    public SimUnitManager GetUnitManager() => _unitManager ?? throw new InvalidOperationException("No UnitManager");
    public SimTerrain GetTerrain() => _terrain ?? throw new InvalidOperationException("No Terrain");

    public EntityHandle GetSystemEntity() => _systemEntity;
    public void SetSystemEntity(EntityHandle h) => _systemEntity = h;

    public int GetCurrentDisplayedPlayer() => -1;

    // Convenience: wire ComponentManager + SystemEntity together (mirrors CSimContext::SetComponentManager flow).
    public void Attach(ComponentManager cm)
    {
        SetComponentManager(cm);
        if (cm.EntityExists(Entity.SystemEntity))
            _systemEntity = cm.GetSystemEntity();
    }
}
