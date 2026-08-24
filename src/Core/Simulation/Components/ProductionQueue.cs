/// Ported from /source/mods/public/simulation/components/ProductionQueue.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Production queue — mirrors ProductionQueue.js Item + timeout progress.</summary>
public sealed class ProductionQueueComponent
{
    public const int MaxQueueSize = 16;
    public const float ProgressIntervalMs = 1000f;

    public sealed class QueueItem
    {
        public int Id;
        public string Template = "";
        public string Type = ""; // "unit" | "technology"
        public int Count = 1;
        public float TimeRemaining;
        public float TimeTotal;
        public bool Started;
        public bool Paused;
        public bool Finished => TimeRemaining <= 0;
        public Dictionary<string,int> Cost = new();
    }

    private readonly List<QueueItem> _queue = new();
    private int _nextId = 1;
    private float _timer;
    public bool AutoQueue;
    public bool Paused;

    public IReadOnlyList<QueueItem> Queue => _queue;
    public bool HasQueued => _queue.Count > 0;
    public bool IsAutoQueueing => AutoQueue;

    public bool AddItem(string template,string type,int count,float buildTime,Dictionary<string,int>? cost,bool pushFront=false)
    {
        if (_queue.Count >= MaxQueueSize) return false;
        var it = new QueueItem { Id=_nextId++, Template=template, Type=type, Count=count, TimeTotal=buildTime*1000f, TimeRemaining=buildTime*1000f, Cost=cost??new() };
        if (pushFront) { if (_queue.Count>0) _queue[0].Paused=true; _queue.Insert(0,it); }
        else _queue.Add(it);
        return true;
    }
    public void RemoveItem(int id)
    {
        int idx=_queue.FindIndex(q=>q.Id==id);
        if(idx>=0) _queue.RemoveAt(idx);
    }
    public void Reset() { _queue.Clear(); AutoQueue=false; Paused=false; _timer=0; }

    /// <summary>Call from GameSim.Tick with dt in seconds. Returns finished items.</summary>
    public List<QueueItem> Tick(float dt, Func<QueueItem,bool>? canPay=null, Action<QueueItem>? onFinished=null)
    {
        var done=new List<QueueItem>();
        if (Paused || _queue.Count==0) return done;
        float budget = dt*1000f;
        while (budget>0 && _queue.Count>0)
        {
            var cur=_queue[0];
            if (cur.Paused) { cur.Paused=false; }
            if (!cur.Started) cur.Started=true;
            if (canPay!=null && !canPay(cur)) break;
            float use=Math.Min(budget, cur.TimeRemaining);
            cur.TimeRemaining-=use; budget-=use;
            if (cur.TimeRemaining<=0.001f)
            {
                _queue.RemoveAt(0);
                done.Add(cur);
                onFinished?.Invoke(cur);
                if (AutoQueue && cur.Type=="unit")
                    AddItem(cur.Template,"unit",cur.Count,cur.TimeTotal/1000f,cur.Cost);
                else if (done.Count>0 && AutoQueue) break; // one per tick like 0 A.D.
            }
            else break;
        }
        return done;
    }
    public void PauseProduction() { Paused=true; if(_queue.Count>0) _queue[0].Paused=true; }
    public void UnpauseProduction() { Paused=false; }
}
