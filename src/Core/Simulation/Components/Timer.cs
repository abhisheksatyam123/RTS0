/// Ported from mods/public/simulation/components/Timer.js + Trigger.js + TriggerPoint.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Timer.js — system timer queue.</summary>
public sealed class TimerComponent
{
    public sealed class TimerEntry
    {
        public int Entity; public string Iid=""; public string Func=""; public float FireAt; public float Repeat; public object? Data;
    }
    private int _nextId=1;
    public float TimeMs;
    public float TurnLengthMs;
    private readonly Dictionary<int,TimerEntry> _timers=new();
    public IReadOnlyDictionary<int,TimerEntry> Timers=>_timers;

    public float GetTime()=>TimeMs;
    public float GetLatestTurnLength()=>TurnLengthMs;
    public int SetTimeout(int ent,string iid,string func,float delay,object? data)
        => SetInterval(ent,iid,func,delay,0,data);
    public int SetInterval(int ent,string iid,string func,float delay,float repeat,object? data)
    {
        int id=_nextId++;
        _timers[id]=new TimerEntry{ Entity=ent,Iid=iid,Func=func,FireAt=TimeMs+delay,Repeat=repeat,Data=data };
        return id;
    }
    public void UpdateRepeat(int id,float newRepeat){ if(_timers.TryGetValue(id,out var t)) t.Repeat=newRepeat; }
    public void CancelTimer(int id)=>_timers.Remove(id);

    public List<int> OnUpdate(float turnLengthSec, System.Func<TimerEntry,float,bool>? invoke=null)
    {
        TurnLengthMs=turnLengthSec*1000f; TimeMs+=TurnLengthMs;
        var run=new List<int>();
        foreach(var kv in _timers) if(kv.Value.FireAt<=TimeMs) run.Add(kv.Key);
        var fired=new List<int>();
        foreach(var id in run)
        {
            if(!_timers.TryGetValue(id,out var t)) continue;
            float lateness=TimeMs-t.FireAt;
            bool ok=invoke?.Invoke(t,lateness) ?? true;
            fired.Add(id);
            if(t.Repeat==0) _timers.Remove(id);
            else { t.FireAt+=t.Repeat; if(t.FireAt<=TimeMs) run.Add(id); }
        }
        return fired;
    }
}

/// <summary>Mirrors Trigger.js — event -> trigger map, range/interval special casing.</summary>
public sealed class TriggerComponent
{
    public static readonly string[] EventNames = new[]{
        "OnAttackDetected","OnCinemaPathEnded","OnConstructionStarted","OnDiplomacyChanged",
        "OnDeserialized","OnInitGame","OnInterval","OnEntityRenamed","OnOwnershipChanged",
        "OnPlayerDefeated","OnPlayerWon","OnRange","OnResearchFinished","OnTrainingFinished","OnTreasureCollected"
    };
    public sealed class TriggerData
    {
        public bool Enabled; public string Action=""; public float Interval,Delay;
        public List<int> Entities=new(); public List<int> Players=new();
        public float MinRange,MaxRange; public int RequiredComponent;
        public int? Timer; public List<int> Queries=new();
        public object? CustomData;
    }
    private readonly Dictionary<string,Dictionary<string,TriggerData>> _triggers=new();
    private readonly Dictionary<string,List<int>> _points=new();
    public string? Difficulty;

    public TriggerComponent(){ foreach(var e in EventNames) _triggers[e]=new(); }

    public void RegisterTriggerPoint(string refs,int ent){ if(!_points.TryGetValue(refs,out var l)) _points[refs]=l=new(); l.Add(ent); }
    public void RemoveTriggerPoint(string refs,int ent){ if(_points.TryGetValue(refs,out var l)) l.Remove(ent); }
    public List<int> GetTriggerPoints(string refs)=> _points.TryGetValue(refs,out var l)? new List<int>(l): new();
    public void RegisterTrigger(string ev,string name,TriggerData data,object? custom=null)
    {
        if(!_triggers.ContainsKey(ev)) return;
        if(_triggers[ev].ContainsKey(name)) return;
        if(string.IsNullOrEmpty(data.Action)) data.Action=name;
        data.CustomData=custom; _triggers[ev][name]=data;
        if(data.Enabled) EnableTrigger(ev,name,null,null);
    }
    public void DisableTrigger(string ev,string name, System.Action<int>? cancel=null, System.Action<int>? disableQ=null)
    {
        if(!_triggers.TryGetValue(ev,out var m)||!m.TryGetValue(name,out var d)) return;
        if(ev=="OnInterval" && d.Timer.HasValue){ cancel?.Invoke(d.Timer.Value); d.Timer=null; }
        if(ev=="OnRange") foreach(var q in d.Queries) disableQ?.Invoke(q);
        d.Enabled=false;
    }
    public void EnableTrigger(string ev,string name, System.Func<float,float,int>? setInterval=null, System.Action<int>? enableQ=null)
    {
        if(!_triggers.TryGetValue(ev,out var m)||!m.TryGetValue(name,out var d)) return;
        if(ev=="OnInterval" && !d.Timer.HasValue && d.Interval>0) d.Timer=setInterval?.Invoke(d.Delay,d.Interval);
        if(ev=="OnRange") foreach(var q in d.Queries) enableQ?.Invoke(q);
        d.Enabled=true;
    }
    public void CallEvent(string ev, object? data, System.Action<string,object?,object?,TriggerData>? doAction=null)
    {
        if(!_triggers.TryGetValue(ev,out var m)) return;
        foreach(var kv in m) if(kv.Value.Enabled) doAction?.Invoke(kv.Value.Action,data,kv.Value.CustomData,kv.Value);
    }
    public string? GetDifficulty()=>Difficulty;
    public void SetDifficulty(string d)=>Difficulty=d;
}

/// <summary>Mirrors TriggerPoint.js — range query helper.</summary>
public sealed class TriggerPointComponent
{
    public string? Reference;
    private readonly Dictionary<int,List<int>> _collections=new();
    private readonly Dictionary<int,string> _triggers=new();

    public TriggerPointComponent(string? refs=null)=>Reference=refs;

    public int RegisterRangeTrigger(string name, TriggerComponent.TriggerData data, System.Func<int,float,float,List<int>,int,string,int>? createQuery=null)
    {
        int tag=createQuery?.Invoke(0,data.MinRange,data.MaxRange,data.Players,data.RequiredComponent,"") ?? Random.Shared.Next(1,999999);
        _collections[tag]=new(); _triggers[tag]=name;
        return tag;
    }
    public void OnRangeUpdate(int tag, List<int> added, List<int> removed, System.Action<string,object>? callTrigger=null)
    {
        if(!_collections.TryGetValue(tag,out var col)) return;
        foreach(var e in removed) col.Remove(e);
        foreach(var e in added) col.Add(e);
        callTrigger?.Invoke(_triggers[tag], new{ currentCollection=new List<int>(col), added, removed });
    }
}
