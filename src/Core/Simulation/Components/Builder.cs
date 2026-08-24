/// Ported from /source/mods/public/simulation/components/Builder.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Builder.js — construction speed + buildable entities list + repair loop.</summary>
public sealed class BuilderComponent
{
    public const float BuildIntervalMs = 1000f;
    public float Rate = 1f;
    public List<string> BuildableEntities = new();
    public int? TargetEntity; // foundation / repairable target
    private float _timer;

    public BuilderComponent(float rate=1f, IEnumerable<string>? entities=null)
    {
        Rate = rate;
        if (entities!=null) BuildableEntities.AddRange(entities);
    }

    public IReadOnlyList<string> GetEntitiesList() => BuildableEntities;
    public float GetRate() => Rate;
    public (float min,float max) GetRange(float obstructionSize=0) => (0, 2 + obstructionSize);

    public bool CanRepair(int target, System.Func<int,bool>? isFoundationOrRepairable=null, System.Func<int,int,bool>? isAlly=null, int owner=0)
    {
        if (isFoundationOrRepairable!=null && !isFoundationOrRepairable(target)) return false;
        if (isAlly!=null && !isAlly(owner,target)) return false;
        return true;
    }
    public bool StartRepairing(int target)
    {
        if (TargetEntity!=null) StopRepairing();
        TargetEntity=target; _timer=0; return true;
    }
    public void StopRepairing(string? reason=null) { TargetEntity=null; _timer=0; }

    /// <summary>Call each tick; invokes buildAction when interval elapses and target in range.</summary>
    public bool Tick(float dt, System.Func<int,bool> inRange, System.Action<int,float> buildAction)
    {
        if (TargetEntity==null) return false;
        int t=TargetEntity.Value;
        if (!inRange(t)) { StopRepairing("OutOfRange"); return false; }
        _timer+=dt*1000f;
        if (_timer>=BuildIntervalMs) { _timer-=BuildIntervalMs; buildAction(t, Rate); return true; }
        return false;
    }
}
