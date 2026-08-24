/// Ported from mods/public/simulation/components/Upgrade.js + Researcher.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Upgrade.js — entity template switching with cost/time.</summary>
public sealed class UpgradeComponent
{
    public sealed class Choice
    {
        public string Entity=""; public string? Icon,Variant,Tooltip;
        public float Time; public Dictionary<string,int> Cost=new(); public bool CheckPlacement;
    }
    public Dictionary<string,Choice> TemplateChoices=new(); // key = internal choice id
    public Dictionary<string,string> UpgradeTemplates=new(); // templateName -> choiceId
    public string? Upgrading; public int ElapsedMs; public Dictionary<string,int> Expended=new();
    public int Owner=-1; public int? TimerId;
    private const int IntervalMs=250;

    public UpgradeComponent(Dictionary<string,Choice>? choices=null){ if(choices!=null) TemplateChoices=new(choices); }

    public void DetermineUpgrades(string nativeCiv,string playerCiv, System.Func<string,bool> exists)
    {
        UpgradeTemplates.Clear();
        foreach(var kv in TemplateChoices)
        {
            string name=kv.Value.Entity.Replace("{native}",nativeCiv).Replace("{civ}",playerCiv);
            if(!exists(name)) continue;
            UpgradeTemplates[name]=kv.Key;
        }
    }
    public bool CanUpgradeTo(string tmpl)=>UpgradeTemplates.ContainsKey(tmpl);
    public List<(string entity,string? icon,Dictionary<string,int>? cost,float? time)> GetUpgrades()
    {
        var r=new List<(string,string?,Dictionary<string,int>?,float?)>();
        foreach(var kv in UpgradeTemplates)
        {
            var c=TemplateChoices[kv.Value];
            Dictionary<string,int>? cost=c.Cost.Count>0? new(c.Cost):null;
            float? t=c.Time>0?c.Time:null;
            r.Add((kv.Key,c.Icon,cost,t));
        }
        return r;
    }
    public bool IsUpgrading()=>Upgrading!=null;
    public string? GetUpgradingTo()=>Upgrading;
    public float GetProgress(float timeSec)=> IsUpgrading()? (timeSec==0?1:Math.Min(ElapsedMs/1000f/timeSec,1)):0;
    public bool Upgrade(string tmpl, System.Func<Dictionary<string,int>,bool> tryPay, System.Func<int,int>? setTimer=null, System.Action<string>? selectAnim=null)
    {
        if(IsUpgrading()||!CanUpgradeTo(tmpl)) return false;
        var choice=TemplateChoices[UpgradeTemplates[tmpl]];
        Expended=new Dictionary<string,int>(choice.Cost);
        if(Expended.Count>0 && !tryPay(Expended)){ Expended.Clear(); return false; }
        Upgrading=tmpl;
        if(!string.IsNullOrEmpty(choice.Variant)) selectAnim?.Invoke(choice.Variant);
        if(choice.Time!=0) TimerId=setTimer?.Invoke(IntervalMs);
        else UpgradeProgress(0);
        return true;
    }
    public void CancelUpgrade(System.Action<Dictionary<string,int>>? refund=null, System.Action? clearAnim=null)
    {
        if(!IsUpgrading()) return;
        refund?.Invoke(Expended); Expended.Clear();
        if(Upgrading!=null && TemplateChoices.TryGetValue(UpgradeTemplates[Upgrading],out var ch) && ch.Variant!=null) clearAnim?.Invoke();
        Upgrading=null; TimerId=null; ElapsedMs=0;
    }
    public void UpgradeProgress(int lateness, System.Action<int>? cancel=null, System.Func<string,int>? changeTemplate=null)
    {
        if(Upgrading==null) return;
        float need=TemplateChoices[UpgradeTemplates[Upgrading]].Time;
        if(ElapsedMs/1000f < need){ ElapsedMs+=IntervalMs+lateness; return; }
        if(TimerId.HasValue) cancel?.Invoke(TimerId.Value);
        TimerId=null; string done=Upgrading; Upgrading=null; Expended.Clear(); ElapsedMs=0;
        changeTemplate?.Invoke(done);
    }
}

/// <summary>Mirrors Researcher.js — queued technology research.</summary>
public sealed class ResearcherComponent
{
    public sealed class Item
    {
        public string Template=""; public int ResearcherId; public object? Metadata;
        public int Player; public bool Started,Paused,Finished;
        public bool Queue(Dictionary<string,float> mult, System.Func<string,int,float,bool> queuedResearch)
            => queuedResearch(Template,ResearcherId,0);
        public void Stop(System.Action<string> stopped)=>stopped(Template);
        public float Progress(float dt, System.Func<string,float,float> progress, System.Func<string,bool> isQueued)
        {
            if(!Started) Started=true; if(Paused) Paused=false;
            float used=progress(Template,dt);
            if(!isQueued(Template)) Finished=true;
            return used;
        }
        public void Pause(System.Action<string> p){ p(Template); Paused=true; }
    }
    private int _nextId=1;
    private readonly Dictionary<int,Item> _queue=new();
    public IReadOnlyDictionary<int,Item> Queue=>_queue;
    public string TechsString=""; public Dictionary<string,float> TechCostMult=new();

    public List<string> GetTechnologiesList(string ownerCiv, System.Func<string,bool> techExists, System.Func<string,bool> canResearch, System.Func<string,bool> isDisabled)
    {
        if(string.IsNullOrWhiteSpace(TechsString)) return new();
        var list=new List<string>(TechsString.Split(' ',System.StringSplitOptions.RemoveEmptyEntries));
        for(int i=0;i<list.Count;i++) if(list[i].Contains("{civ}"))
        { string ct=list[i].Replace("{civ}",ownerCiv); list[i]=techExists(ct)?ct:list[i].Replace("{civ}","generic"); }
        list.RemoveAll(t=> isDisabled(t)||!canResearch(t));
        return list;
    }
    public int QueueTechnology(string tmpl, object? meta,int selfId, Dictionary<string,float> mult, System.Func<string,int,Dictionary<string,float>,bool> tryQueue)
    {
        var it=new Item{ Template=tmpl,ResearcherId=selfId,Metadata=meta,Player=selfId };
        if(!tryQueue(tmpl,selfId,mult)) return -1;
        int id=_nextId++; _queue[id]=it; return id;
    }
    public void StopResearching(int id, System.Action<string> stopped){ if(_queue.TryGetValue(id,out var it)){ it.Stop(stopped); _queue.Remove(id);} }
    public float Progress(int id,float dt, System.Func<string,float,float> prog, System.Func<string,bool> isQueued)
    {
        if(!_queue.TryGetValue(id,out var it)) return 0;
        float u=it.Progress(dt,prog,isQueued);
        if(it.Finished) _queue.Remove(id);
        return u;
    }
}
