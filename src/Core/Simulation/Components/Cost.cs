/// Ported from /source/mods/public/simulation/components/Cost.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Cost.js — population, build time, resource costs.</summary>
public sealed class CostComponent
{
    public int Population = 1;
    public float BuildTime = 10f;
    public Dictionary<string,int> Resources = new();

    public CostComponent(int pop=1,float time=10f, Dictionary<string,int>? res=null)
    {
        Population=pop; BuildTime=time;
        if(res!=null) Resources=new Dictionary<string,int>(res);
    }
    public int GetPopCost()=>Population;
    public float GetBuildTime()=>BuildTime;
    public Dictionary<string,int> GetResourceCosts()=>new(Resources);

    public bool CanAfford(PlayerComponent player)
    {
        foreach(var kv in Resources) if(player.ResourceCount.GetValueOrDefault(kv.Key) < kv.Value) return false;
        return player.TryReservePop(Population)==0 ? true : false;
    }
    // Note: caller should un-reserve if CanAfford fails due to pop
}
