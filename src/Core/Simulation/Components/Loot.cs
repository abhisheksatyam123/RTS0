/// Ported from /source/mods/public/simulation/components/Loot.js + Looter.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Loot.js — xp + resource bounty on death.</summary>
public sealed class LootComponent
{
    public int Xp;
    public Dictionary<string,int> Resources=new();
    public LootComponent(int xp=0, Dictionary<string,int>? res=null){ Xp=xp; if(res!=null) Resources=new(res); }
    public int GetXp()=>Xp;
    public Dictionary<string,int> GetResources()=>new(Resources);
}

/// <summary>Mirrors Looter.js — collects Loot + carried resources.</summary>
public sealed class LooterComponent
{
    public void Collect(int target, LootComponent? loot, Dictionary<string,int>? carried, PlayerComponent owner, Dictionary<string,float> multipliers=null!)
    {
        var gain=new Dictionary<string,int>();
        if(loot!=null) foreach(var kv in loot.Resources) gain[kv.Key]=gain.GetValueOrDefault(kv.Key)+(int)(kv.Value * (multipliers?.GetValueOrDefault(kv.Key,1f)??1f));
        if(carried!=null) foreach(var kv in carried) gain[kv.Key]=gain.GetValueOrDefault(kv.Key)+kv.Value;
        if(gain.Count>0) owner.AddResources(gain);
    }
}
