/// Ported from /source/mods/public/simulation/components/ResourceTrickle.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors ResourceTrickle.js — periodic resource income to owner.</summary>
public sealed class ResourceTrickleComponent
{
    public Dictionary<string,float> Rates = new();
    public float IntervalMs = 1000f;
    private float _accum;

    public ResourceTrickleComponent(Dictionary<string,float>? rates=null,float intervalMs=1000f)
    {
        if(rates!=null) Rates=new Dictionary<string,float>(rates);
        IntervalMs=intervalMs;
    }
    public float GetInterval()=>IntervalMs;
    public IReadOnlyDictionary<string,float> GetRates()=>Rates;
    public bool HasTrickle()
    {
        foreach(var v in Rates.Values) if(v>0) return true;
        return false;
    }
    /// <summary>Tick from GameSim; returns resources to credit when interval elapsed.</summary>
    public Dictionary<string,int>? Tick(float dt)
    {
        if(!HasTrickle() || IntervalMs<=0) return null;
        _accum+=dt*1000f;
        if(_accum < IntervalMs) return null;
        _accum-=IntervalMs;
        var ret=new Dictionary<string,int>();
        foreach(var kv in Rates) if(kv.Value>0) ret[kv.Key]=(int)kv.Value;
        return ret;
    }
    public void ApplyTo(PlayerComponent player, Dictionary<string,int> amounts) => player.AddResources(amounts);
}
