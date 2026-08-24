/// Ported from mods/public/simulation/components/ResourceDropsite.js + Treasure.js + TreasureCollector.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors ResourceDropsite.js — sharable resource reception.</summary>
public sealed class ResourceDropsiteComponent
{
    public HashSet<string> Types=new();
    public bool Sharable, Shared;

    public ResourceDropsiteComponent(string types="",bool sharable=false)
    {
        foreach(var t in types.Split(' ',System.StringSplitOptions.RemoveEmptyEntries)) Types.Add(t);
        Sharable=sharable; Shared=sharable;
    }
    public List<string> GetTypes()=>new(Types);
    public bool AcceptsType(string t)=>Types.Contains(t);
    public Dictionary<string,int> ReceiveResources(Dictionary<string,int> resources, System.Action<Dictionary<string,int>> addToPlayer)
    {
        var taken=new Dictionary<string,int>();
        foreach(var kv in resources) if(AcceptsType(kv.Key)) taken[kv.Key]=kv.Value;
        if(taken.Count>0) addToPlayer(taken);
        return taken;
    }
    public bool IsSharable()=>Sharable;
    public bool IsShared()=>Shared;
    public void SetSharing(bool v, System.Action<bool>? broadcast=null)
    { if(!Sharable) return; Shared=v; broadcast?.Invoke(v); }
}

/// <summary>Mirrors Treasure.js — collectable resource stash.</summary>
public sealed class TreasureComponent
{
    public float CollectTimeMs=1000;
    public Dictionary<string,int> Resources=new();
    public bool IsTaken;

    public TreasureComponent(float collectTime=1000, Dictionary<string,int>? res=null)
    { CollectTimeMs=collectTime; if(res!=null) Resources=new(res); }

    public Dictionary<string,int> GetResources()=>Resources;
    public float CollectionTime()=>CollectTimeMs;
    public bool IsAvailable()=>!IsTaken;
    public bool Reward(int collectorOwner, System.Action<Dictionary<string,int>> addToPlayer, System.Action? onCollected=null)
    {
        if(IsTaken) return false;
        if(Resources.Count>0) addToPlayer(new Dictionary<string,int>(Resources));
        onCollected?.Invoke();
        IsTaken=true;
        return true;
    }
}

/// <summary>Mirrors TreasureCollector.js — moves to treasure and collects after timer.</summary>
public sealed class TreasureCollectorComponent
{
    public float MaxDistance=2f;
    public int? Target; public int? TimerId; public string? CallerIid;

    public TreasureCollectorComponent(float maxDist=2f)=>MaxDistance=maxDist;
    public (float min,float max) GetRange()=>(0,MaxDistance);
    public bool CanCollect(int target, System.Func<int,bool> isAvailable)=> isAvailable(target);
    public bool StartCollecting(int target,string callerIid, System.Func<int,bool> isAvailable, System.Func<float,int>? setTimeout=null, System.Action<string>? anim=null)
    {
        if(Target.HasValue) StopCollecting();
        if(!isAvailable(target)) return false;
        anim?.Invoke("collecting_treasure");
        Target=target; CallerIid=callerIid;
        TimerId=setTimeout?.Invoke(MaxDistance*500); // caller supplies real CollectTime
        return true;
    }
    public void StopCollecting(string? reason=null, System.Action<int>? cancel=null, System.Action<string>? anim=null, System.Action<string>? notifyCaller=null)
    {
        if(!Target.HasValue) return;
        if(TimerId.HasValue) cancel?.Invoke(TimerId.Value);
        TimerId=null; Target=null;
        anim?.Invoke("idle");
        string? iid=CallerIid; CallerIid=null;
        if(reason!=null && iid!=null) notifyCaller?.Invoke(reason);
    }
    public void CollectTreasure(System.Func<int,bool> isAvailable, System.Func<int,bool> inRange, System.Func<int,Dictionary<string,int>,bool> reward, System.Action<int>? cancel=null)
    {
        if(!Target.HasValue) return;
        int t=Target.Value;
        if(!isAvailable(t)){ StopCollecting("TargetInvalidated",cancel); return; }
        if(!inRange(t)){ StopCollecting("OutOfRange",cancel); return; }
        reward(t,new Dictionary<string,int>());
        StopCollecting("TargetInvalidated",cancel);
    }
    public bool IsTargetInRange(int target, System.Func<int,int,float,float,bool> isInRange)
    { var r=GetRange(); return isInRange(-1,target,r.min,r.max); }
}
