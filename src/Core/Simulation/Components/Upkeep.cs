/// Ported from mods/public/simulation/components/Upkeep.js + StatisticsTracker.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

/// <summary>Mirrors Upkeep.js — periodic resource drain.</summary>
public sealed class UpkeepComponent
{
    public Dictionary<string,float> Rates = new();
    public float IntervalMs = 10000;
    public int? TimerId;
    public bool Unpayed;

    public UpkeepComponent(Dictionary<string,float>? rates=null,float interval=10000)
    {
        if(rates!=null) Rates=new Dictionary<string,float>(rates);
        IntervalMs=interval;
    }
    public float GetInterval()=>IntervalMs;
    public IReadOnlyDictionary<string,float> GetRates()=>Rates;
    public bool HasUpkeep()=>Rates.Count>0 && Rates.Values.Any(v=>v>0);

    public bool TryPay(System.Func<Dictionary<string,float>,bool> trySubtract, System.Action<bool>? onControllable=null)
    {
        if(!HasUpkeep()) return true;
        // convert float rates to int-like via caller; here use float dict directly
        var asFloat = new Dictionary<string,float>(Rates);
        bool ok=trySubtract(asFloat);
        if(!ok && !Unpayed){ Unpayed=true; onControllable?.Invoke(false); }
        else if(ok && Unpayed){ Unpayed=false; onControllable?.Invoke(true); }
        return ok;
    }
    public void PayTick(System.Func<Dictionary<string,float>,bool> trySubtract, System.Action<bool>? onControllable=null)
        => TryPay(trySubtract,onControllable);

    public void CheckTimer(System.Func<float,float> modInterval, System.Action<float>? setTimer, System.Action<int>? cancel)
    {
        if(!HasUpkeep()){ if(TimerId.HasValue) cancel?.Invoke(TimerId.Value); TimerId=null; return; }
        float old=IntervalMs;
        IntervalMs=modInterval(IntervalMs);
        if(IntervalMs<0){ if(TimerId.HasValue) cancel?.Invoke(TimerId.Value); TimerId=null; return; }
        // caller handles create/update
    }
}

/// <summary>Mirrors StatisticsTracker.js — counters for units/buildings/resources.</summary>
public sealed class StatisticsTrackerComponent
{
    public Dictionary<string,int> UnitsTrained=new(), UnitsLost=new(), EnemyUnitsKilled=new(), UnitsCaptured=new();
    public int UnitsLostValue, EnemyUnitsKilledValue, UnitsCapturedValue;
    public Dictionary<string,int> BuildingsConstructed=new(), BuildingsLost=new(), EnemyBuildingsDestroyed=new(), BuildingsCaptured=new();
    public int BuildingsLostValue, EnemyBuildingsDestroyedValue, BuildingsCapturedValue;
    public Dictionary<string,int> ResourcesGathered=new(), ResourcesUsed=new(), ResourcesSold=new(), ResourcesBought=new();
    public int TributesSent, TributesReceived, TradeIncome, TreasuresCollected, LootCollected;
    public int SuccessfulBribes, FailedBribes;
    public float PeakMapControlled, TeamPeakMapControlled;

    private readonly string[] _unitClasses, _structClasses;
    public StatisticsTrackerComponent(string[]? unitClasses=null,string[]? structClasses=null)
    {
        _unitClasses=unitClasses??new[]{"Infantry","Cavalry","Support"};
        _structClasses=structClasses??new[]{"House","Wonder","Fortress"};
        foreach(var c in _unitClasses){ UnitsTrained[c]=0; UnitsLost[c]=0; EnemyUnitsKilled[c]=0; UnitsCaptured[c]=0; }
        foreach(var c in _structClasses){ BuildingsConstructed[c]=0; BuildingsLost[c]=0; EnemyBuildingsDestroyed[c]=0; BuildingsCaptured[c]=0; }
        foreach(var r in new[]{"food","wood","stone","metal"}){ ResourcesGathered[r]=0; ResourcesUsed[r]=0; ResourcesSold[r]=0; ResourcesBought[r]=0; }
        ResourcesGathered["vegetarianFood"]=0;
    }
    private void Inc(HashSet<string> classes, Dictionary<string,int> counter,string type)
        { if(classes.Contains(type)) counter[type]++; }

    public void IncreaseTrained(HashSet<string> classes, Dictionary<string,int>? cost=null)
    {
        foreach(var t in _unitClasses) Inc(classes,UnitsTrained,t);
        if(!classes.Contains("Domestic")) UnitsTrained["total"]=UnitsTrained.GetValueOrDefault("total")+1;
    }
    public void IncreaseConstructed(HashSet<string> classes)
    {
        foreach(var t in _structClasses) Inc(classes,BuildingsConstructed,t);
        BuildingsConstructed["total"]=BuildingsConstructed.GetValueOrDefault("total")+1;
    }
    public void KilledEntity(HashSet<string> classes, Dictionary<string,int>? cost,bool isUnit,bool isStructure,bool isFoundation)
    {
        if(isUnit){ foreach(var t in _unitClasses) Inc(classes,EnemyUnitsKilled,t); if(cost!=null) foreach(var v in cost.Values) EnemyUnitsKilledValue+=v; }
        if(isStructure && !isFoundation){ foreach(var t in _structClasses) Inc(classes,EnemyBuildingsDestroyed,t); if(cost!=null) foreach(var v in cost.Values) EnemyBuildingsDestroyedValue+=v; }
    }
    public void LostEntity(HashSet<string> classes, Dictionary<string,int>? cost,bool isUnit,bool isStructure,bool isFoundation)
    {
        if(isUnit && !classes.Contains("Domestic")){ foreach(var t in _unitClasses) Inc(classes,UnitsLost,t); if(cost!=null) foreach(var v in cost.Values) UnitsLostValue+=v; }
        if(isStructure && !isFoundation){ foreach(var t in _structClasses) Inc(classes,BuildingsLost,t); if(cost!=null) foreach(var v in cost.Values) BuildingsLostValue+=v; }
    }
    public void CapturedEntity(HashSet<string> classes, Dictionary<string,int>? cost,bool isUnit,bool isStructure)
    {
        if(isUnit){ foreach(var t in _unitClasses) Inc(classes,UnitsCaptured,t); if(cost!=null) foreach(var v in cost.Values) UnitsCapturedValue+=v; }
        if(isStructure){ foreach(var t in _structClasses) Inc(classes,BuildingsCaptured,t); if(cost!=null) foreach(var v in cost.Values) BuildingsCapturedValue+=v; }
    }
    public void AddGathered(string type,int amt,string? specific=null)
    { ResourcesGathered[type]=ResourcesGathered.GetValueOrDefault(type)+amt; if(type=="food" && (specific=="fruit"||specific=="grain")) ResourcesGathered["vegetarianFood"]+=amt; }
    public void AddUsed(string type,int amt)=> ResourcesUsed[type]=ResourcesUsed.GetValueOrDefault(type)+amt;
    public void AddSold(string type,int amt)=> ResourcesSold[type]=ResourcesSold.GetValueOrDefault(type)+amt;
    public void AddBought(string type,int amt)=> ResourcesBought[type]=ResourcesBought.GetValueOrDefault(type)+amt;
    public void AddTributeSent(int v)=>TributesSent+=v;
    public void AddTributeReceived(int v)=>TributesReceived+=v;
    public void AddTradeIncome(int v)=>TradeIncome+=v;
    public void IncTreasure()=>TreasuresCollected++;
    public void AddLoot(Dictionary<string,int> loot){ foreach(var v in loot.Values) LootCollected+=v; }
}
