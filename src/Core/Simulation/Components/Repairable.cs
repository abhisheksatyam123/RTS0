/// Ported from mods/public/simulation/components/Repairable.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Repairable.js — tracks builders repairing damaged entity.</summary>
public sealed class RepairableComponent
{
    public float RepairTimeRatio = 2f;
    private readonly Dictionary<int,float> _builders = new();
    private float _totalRate;
    private float _buildMultiplier = 1f;
    private const float Penalty = 0.7f;
    private bool _unrepairable;

    public IReadOnlyDictionary<int,float> Builders => _builders;
    public int NumBuilders => _builders.Count;
    public bool IsRepairable => !_unrepairable;
    public void SetRepairability(bool v) => _unrepairable=!v;

    public float GetBuildProgress(int hp,int maxHp) => maxHp<=0?0:(float)hp/maxHp;
    public List<int> GetBuilders() => new(_builders.Keys);

    public void AddBuilder(int ent,float rate)
    {
        if(_builders.ContainsKey(ent)) return;
        _builders[ent]=rate; _totalRate+=rate; UpdateMult();
    }
    public void AddBuilders(IEnumerable<int> ents, System.Func<int,float> getRate)
        { foreach(var e in ents) AddBuilder(e,getRate(e)); }
    public void RemoveBuilder(int ent)
    {
        if(!_builders.TryGetValue(ent,out var r)) return;
        _totalRate-=r; _builders.Remove(ent); UpdateMult();
    }
    private float CalcMult(int n)=> n<2?1:MathF.Pow(n,Penalty)/n;
    private void UpdateMult()=> _buildMultiplier=CalcMult(NumBuilders);

    public (float timeRemaining,float timeRemainingNew) GetBuildTime(int hp,int maxHp,float buildTime)
    {
        float left=(1-GetBuildProgress(hp,maxHp))*buildTime*RepairTimeRatio;
        float rate=_totalRate*_buildMultiplier;
        float rateNew=(_totalRate+1)*CalcMult(NumBuilders+1);
        return (rate>0?left/rate:0, rateNew>0?left/rateNew:0);
    }
    public float GetRepairRate(int maxHp,float buildTime)
    {
        float t=RepairTimeRatio*buildTime;
        return t>0? (float)maxHp/t : 1f;
    }
    /// <summary>Mirrors Repairable.Repair — heals target.</summary>
    public int Repair(int builderEnt,float rate,int hp,int maxHp, System.Action<int> heal)
    {
        int dmg=maxHp-hp; if(dmg<=0) return 0;
        float work=rate*_buildMultiplier*GetRepairRate(maxHp,1); // buildTime injected via heal calc caller
        // Use actual buildTime via delegate? Simplified: work as hp
        int amount=Math.Min(dmg,(int)work);
        if(amount>0) heal(amount);
        if(_builders.ContainsKey(builderEnt)){ _totalRate+=rate-_builders[builderEnt]; _builders[builderEnt]=rate; }
        return amount;
    }
    public int RepairWithTime(int builderEnt,float rate,int hp,int maxHp,float buildTime, System.Action<int> heal)
    {
        int dmg=maxHp-hp; if(dmg<=0) return 0;
        float work=rate*_buildMultiplier*GetRepairRate(maxHp,buildTime);
        int amount=Math.Min(dmg,(int)work);
        if(amount>0) heal(amount);
        if(_builders.ContainsKey(builderEnt)){ _totalRate+=rate-_builders[builderEnt]; _builders[builderEnt]=rate; }
        else { _builders[builderEnt]=rate; _totalRate+=rate; UpdateMult(); }
        return amount;
    }
}

/// <summary>Mirage helper mirrors RepairableMirage.</summary>
public sealed class RepairableMirage
{
    public int NumBuilders; public (float a,float b) BuildTime; public bool Unrepairable;
    public void Init(RepairableComponent r,int hp,int maxHp,float bt)
    { NumBuilders=r.NumBuilders; BuildTime=r.GetBuildTime(hp,maxHp,bt); Unrepairable=!r.IsRepairable; }
    public bool IsRepairable => !Unrepairable;
}
