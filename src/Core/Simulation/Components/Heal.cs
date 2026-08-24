/// Ported from /source/mods/public/simulation/components/Heal.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Heal.js — range, rate, class filtering, heal loop.</summary>
public sealed class HealComponent
{
    public float Range = 10f;
    public float Health = 5f;
    public float IntervalMs = 1000f;
    public HashSet<string> HealableClasses = new();
    public HashSet<string> UnhealableClasses = new();
    private int? _target;
    private float _timer;
    private float _lastHealed;

    public HealComponent(float range=10,float hp=5,float interval=1000, IEnumerable<string>? healable=null, IEnumerable<string>? unhealable=null)
    {
        Range=range; Health=hp; IntervalMs=interval;
        if(healable!=null) HealableClasses=new HashSet<string>(healable);
        if(unhealable!=null) UnhealableClasses=new HashSet<string>(unhealable);
    }
    public (float min,float max) GetRange()=>(0,Range);
    public float GetHealth()=>Health;
    public float GetInterval()=>IntervalMs;

    public bool CanHeal(int target, HealthComponent health, HashSet<string> targetClasses, int owner, int healerOwner, System.Func<int,int,bool> isAlly)
    {
        if(health.IsDead || !health.IsInjured) return false;
        if(!isAlly(healerOwner,target)) return false;
        foreach(var c in UnhealableClasses) if(targetClasses.Contains(c)) return false;
        if(HealableClasses.Count==0) return true;
        foreach(var c in HealableClasses) if(targetClasses.Contains(c)) return true;
        return false;
    }
    public bool StartHealing(int target){ _target=target; _timer=1000f; return true; }
    public void StopHealing(){ _target=null; _timer=0; }

    /// <summary>Tick; returns true when a heal was applied.</summary>
    public bool Tick(float dt, System.Func<int,bool> inRange, System.Func<int,HealthComponent?> getHealth)
    {
        if(_target==null) return false;
        int t=_target.Value;
        var h=getHealth(t);
        if(h==null || h.IsDead || !h.IsInjured){ StopHealing(); return false; }
        if(!inRange(t)){ StopHealing(); return false; }
        _timer-=dt*1000f;
        if(_timer<=0){ _timer+=IntervalMs; h.Heal((int)Health); _lastHealed+=dt; if(!h.IsInjured) StopHealing(); return true; }
        return false;
    }
}
