/// Ported from mods/public/simulation/components/Foundation.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Foundation.js — uncommitted/committed, builder tracking, progress via Health.</summary>
public sealed class FoundationComponent
{
    public string FinalTemplate = "";
    public bool Committed;
    public bool Initialised;
    public Dictionary<string,int> Costs = new();
    public float MaxProgress;
    public int PreviewEntity = -1; // INVALID_ENTITY
    public float BuildTimeModifier = 0.7f;

    private readonly Dictionary<int,float> _builders = new(); // entity -> rate
    private float _totalRate;
    private float _buildMultiplier = 1f;

    public IReadOnlyDictionary<int,float> Builders => _builders;
    public int NumBuilders => _builders.Count;
    public bool IsFinished(float progress) => progress >= 1f - 1e-6f;

    public float GetBuildProgress(int hp, int maxHp) => maxHp <= 0 ? 0 : (float)hp / maxHp;
    public int GetBuildPercentage(int hp, int maxHp) => (int)(GetBuildProgress(hp,maxHp)*100);

    public void InitialiseConstruction(string template, Dictionary<string,int> costs)
    {
        FinalTemplate = template;
        Costs = new Dictionary<string,int>(costs);
        MaxProgress = 0;
        Initialised = true;
    }

    public bool AddBuilder(int ent, float rate)
    {
        if (_builders.ContainsKey(ent)) return false;
        _builders[ent] = rate;
        _totalRate += rate;
        UpdateMultiplier();
        return true;
    }
    public void AddBuilders(IEnumerable<int> ents, System.Func<int,float> getRate)
    {
        bool ch=false;
        foreach(var e in ents) if(AddBuilder(e,getRate(e))) ch=true;
    }
    public void RemoveBuilder(int ent)
    {
        if(!_builders.TryGetValue(ent,out var r)) return;
        _totalRate -= r;
        _builders.Remove(ent);
        UpdateMultiplier();
    }
    public List<int> GetBuilders() => new(_builders.Keys);

    private float CalcMultiplier(int n) => n < 2 ? 1f : MathF.Pow(n, BuildTimeModifier) / n;
    private void UpdateMultiplier() => _buildMultiplier = CalcMultiplier(NumBuilders);

    public (float timeRemaining, float timeRemainingNew) GetBuildTime(int hp,int maxHp,float buildTime)
    {
        float prog = GetBuildProgress(hp,maxHp);
        float left = (1f - prog) * buildTime;
        float rate = _totalRate * _buildMultiplier;
        float rateNew = (_totalRate + 1) * CalcMultiplier(NumBuilders+1);
        return (rate > 0 ? left/rate : 0, rateNew > 0 ? left/rateNew : 0);
    }
    public float GetBuildRate(int maxHp,float buildTime) => buildTime <=0 ? float.PositiveInfinity : (float)maxHp / buildTime;

    public bool Commit(System.Func<bool>? canCommit=null)
    {
        if(Committed) return false;
        if(canCommit!=null && !canCommit()) return false;
        Committed = true;
        return true;
    }
    public float BuildTick(int builderEnt, float work, int hp,int maxHp,float buildTime, System.Action<int> onHpIncrease)
    {
        if(IsFinished(GetBuildProgress(hp,maxHp))) return 0;
        if(!Committed && !Commit()) return 0;
        float delta = work * GetBuildRate(maxHp,buildTime) * _buildMultiplier;
        if(delta>0) onHpIncrease((int)delta);
        // update builder rate
        if(_builders.ContainsKey(builderEnt))
        {
            _totalRate += work - _builders[builderEnt];
            _builders[builderEnt]=work;
        }
        return delta;
    }
}

/// <summary>Mirage helper mirrors FoundationMirage in Foundation.js.</summary>
public sealed class FoundationMirage
{
    public int NumBuilders;
    public (float a,float b) BuildTime;
    public void Init(FoundationComponent f,int hp,int maxHp,float bt) { NumBuilders=f.NumBuilders; BuildTime=f.GetBuildTime(hp,maxHp,bt); }
}
