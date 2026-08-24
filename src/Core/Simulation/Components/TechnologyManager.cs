/// Ported from /source/mods/public/simulation/components/TechnologyManager.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class TechnologyManagerComponent
{
    public sealed class Tech
    {
        public string Name="";
        public Dictionary<string,int> Cost=new();
        public float TimeRemaining, TimeTotal;
        public bool Paid, Started, Finished, Paused;
        public bool Progress(float dt, Func<Dictionary<string,int>,bool> canPay, Action onFinish)
        {
            if(!Paid){ if(!canPay(Cost)) return false; Paid=true; }
            if(!Started) Started=true;
            if(Paused) return false;
            TimeRemaining-=dt*1000f;
            if(TimeRemaining<=0){ Finished=true; onFinish(); return true; }
            return false;
        }
    }

    public HashSet<string> Researched=new();
    public Dictionary<string,Tech> Queued=new();
    public Dictionary<string,int> ClassCounts=new();
    public Dictionary<string,Dictionary<string,int>> TypeCountsByClass=new();
    public HashSet<string> AutoResearchPending=new();

    public bool IsResearched(string t)=>Researched.Contains(t);
    public bool IsQueued(string t)=>Queued.ContainsKey(t);
    public bool IsInProgress(string t)=>Queued.ContainsKey(t);

    public bool CanResearch(string tech, Func<string,bool>? reqCheck=null)
    {
        if(IsResearched(tech)||IsQueued(tech)) return false;
        return reqCheck?.Invoke(tech) ?? true;
    }
    public void MarkResearched(string tech){ Researched.Add(tech); }
    public void ResearchInstant(string tech){ if(IsQueued(tech)||IsResearched(tech)) return; Researched.Add(tech); }

    public bool Queue(string tech, Dictionary<string,int> cost,float timeSec)
    {
        if(IsQueued(tech)||IsResearched(tech)) return false;
        Queued[tech]=new Tech{ Name=tech, Cost=new Dictionary<string,int>(cost), TimeTotal=timeSec*1000f, TimeRemaining=timeSec*1000f };
        return true;
    }
    public float Progress(string tech,float dt, Func<Dictionary<string,int>,bool> canPay)
    {
        if(!Queued.TryGetValue(tech,out var t)) return 0;
        float before=t.TimeRemaining;
        t.Progress(dt, canPay, ()=>{ Queued.Remove(tech); Researched.Add(tech); });
        return before - t.TimeRemaining;
    }
    public void Stop(string tech){ Queued.Remove(tech); }
    public void Pause(string tech){ if(Queued.TryGetValue(tech,out var t)) t.Paused=true; }
    public void Unpause(string tech){ if(Queued.TryGetValue(tech,out var t)) t.Paused=false; }

    public void OnOwnershipChanged(string template, string[] classes, bool gained)
    {
        int d=gained?1:-1;
        foreach(var c in classes)
        {
            ClassCounts[c]=ClassCounts.GetValueOrDefault(c)+d;
            if(ClassCounts[c]<=0) ClassCounts.Remove(c);
            if(!TypeCountsByClass.TryGetValue(c,out var m)) { if(d<0) continue; m=new(); TypeCountsByClass[c]=m; }
            m[template]=m.GetValueOrDefault(template)+d;
            if(m[template]<=0) m.Remove(template);
        }
    }
}
