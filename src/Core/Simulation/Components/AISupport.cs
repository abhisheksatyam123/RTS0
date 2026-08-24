/// Ported from mods/public/simulation/components/AIInterface.js + AIProxy.js + BuildingAI.js + BattleDetection.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors AIInterface.js — system event bus for AI.</summary>
public sealed class AIInterfaceComponent
{
    public static readonly string[] EventNames=new[]{"Create","Destroy","Attacked","ConstructionFinished","DiplomacyChanged","TrainingStarted","TrainingFinished","AIMetadata","PlayerDefeated","EntityRenamed","ValueModification","OwnershipChanged","Garrison","UnGarrison","TerritoriesChanged","TerritoryDecayChanged","TributeExchanged","AttackRequest","CeasefireEnded","DiplomacyRequest","TributeRequest"};
    public Dictionary<string,List<object>> Events=new();
    public Dictionary<int,int> ChangedEntities=new();
    public Dictionary<int,Dictionary<string,List<object>>> ChangedTemplateInfo=new();
    public Dictionary<int,List<object>> ChangedEntityTemplateInfo=new();
    public bool Enabled=true;

    public AIInterfaceComponent(){ foreach(var n in EventNames) Events[n]=new(); }

    public void Disable(){ Enabled=false; }
    public void ChangedEntity(int ent){ if(!Enabled) return; ChangedEntities[ent]=1; }
    public void PushEvent(string type,object msg){ if(!Enabled||!Events.ContainsKey(type)) return; Events[type].Add(msg); }
    public Dictionary<string,List<object>> GetNonEntityRepresentation()
    {
        var state=new Dictionary<string,List<object>>();
        foreach(var kv in Events){ state[kv.Key]=new List<object>(kv.Value); kv.Value.Clear(); }
        return state;
    }
}

/// <summary>Mirrors AIProxy.js — per-entity delta tracking for AI.</summary>
public sealed class AIProxyComponent
{
    public Dictionary<string,object?> Changes=new(); public bool NeedsFullGet=true; public bool Initialised;
    public AIProxyComponent(){ NeedsFullGet=true; }

    public Dictionary<string,object?> GetRepresentation(System.Func<Dictionary<string,object?>> full)
    {
        Dictionary<string,object?> ret=NeedsFullGet? full(): new Dictionary<string,object?>(Changes);
        Changes.Clear(); NeedsFullGet=false; return ret;
    }
    public bool NotifyChange(int owner, AIInterfaceComponent ai,int ent)
    {
        if(NeedsFullGet && owner<0) return false;
        if(Changes.Count==0) ai.ChangedEntity(ent);
        return true;
    }
    public void OnPositionChanged(bool inWorld,float x,float z,float angle, AIInterfaceComponent ai,int ent,int owner)
    { if(!NotifyChange(owner,ai,ent)) return; Changes["position"]= inWorld? new float[]{x,z}:null; Changes["angle"]= inWorld? (object)angle:null; }
    public void OnHealthChanged(int hp, AIInterfaceComponent ai,int ent,int owner)
    { if(!NotifyChange(owner,ai,ent)) return; Changes["hitpoints"]=hp; }
    public void OnOwnershipChanged(int from,int to, AIInterfaceComponent ai,int ent)
    { NotifyChange(to,ai,ent); if(from==-1) ai.PushEvent("Create",new{entity=ent}); else if(to==-1) ai.PushEvent("Destroy",new{entity=ent}); else { Changes["owner"]=to; ai.PushEvent("OwnershipChanged",new{entity=ent,from,to}); } }
    public void OnCapturePointsChanged(List<float> cp, AIInterfaceComponent ai,int ent,int owner)
    { if(!NotifyChange(owner,ai,ent)) return; Changes["capturePoints"]=cp; }

    public Dictionary<string,object?> GetFullRepresentation(int id,string template,float? x,float? z,float? angle,int? hp,int owner)
    {
        NeedsFullGet=false;
        var r=new Dictionary<string,object?>{{"id",id},{"template",template},{"owner",owner}};
        if(x.HasValue) r["position"]=new float[]{x.Value,z!.Value};
        if(angle.HasValue) r["angle"]=angle.Value;
        if(hp.HasValue) r["hitpoints"]=hp.Value;
        return r;
    }
}

/// <summary>Mirrors BuildingAI.js — garrisoned archer firing.</summary>
public sealed class BuildingAIComponent
{
    public int DefaultArrowCount=3, MaxArrowCount=int.MaxValue; public float GarrisonArrowMultiplier=1f; public string GarrisonArrowClasses="Infantry";
    public int ArchersGarrisoned; public int CurrentRound, ArrowsLeft;
    public List<int> TargetUnits=new(), FocusTargets=new(); public int? UnitAITarget;
    public int? TimerId; public int? EnemyQuery, GaiaQuery;

    public void OnGarrisonedUnitsChanged(List<int> added,List<int> removed, System.Func<int,HashSet<string>> getClasses)
    {
        foreach(var e in added) if(Matches(getClasses(e))) ArchersGarrisoned++;
        foreach(var e in removed) if(Matches(getClasses(e))) ArchersGarrisoned--;
    }
    private bool Matches(HashSet<string> cls){ foreach(var c in GarrisonArrowClasses.Split(' ',System.StringSplitOptions.RemoveEmptyEntries)) if(cls.Contains(c)) return true; return false; }
    public int GetArrowCount()
    {
        int c=DefaultArrowCount+(int)MathF.Round(ArchersGarrisoned*GarrisonArrowMultiplier);
        return Math.Min(c,MaxArrowCount);
    }
    public void FireArrows(System.Func<int,bool> canAttack, System.Func<int,int,bool>? inRange=null, System.Action<int>? perform=null)
    {
        if(TargetUnits.Count==0 && UnitAITarget==null) return;
        if(CurrentRound>=20) CurrentRound=0;
        if(CurrentRound==0) ArrowsLeft=GetArrowCount();
        int toFire = CurrentRound==19? ArrowsLeft : Math.Min(GetArrowCount()/5, ArrowsLeft);
        if(toFire<=0){ CurrentRound++; return; }
        var targets=FocusTargets.Count>0? new List<int>(FocusTargets): new List<int>(TargetUnits);
        int fired=0;
        foreach(var t in targets){ if(fired>=toFire) break; if(!canAttack(t)) continue; if(inRange!=null && !inRange(-1,t)) continue; perform?.Invoke(t); fired++; }
        ArrowsLeft-=fired; CurrentRound++;
    }
    public void StartTimer(System.Func<float,float,int>? setInterval=null,float prepare=1000,float repeat=2000)
    { if(TimerId!=null) return; TimerId=setInterval?.Invoke(prepare,repeat/20f); }
}

/// <summary>Mirrors BattleDetection.js — damage rate -> battle/peace state.</summary>
public sealed class BattleDetectionComponent
{
    public float IntervalMs=1000; public int RecordLength=5; public float DamageRateThreshold=1f, AlertBattle=2f, AlertPeace=1f, AlertMax=5f;
    public float Damage; public List<float> DamageRecord=new(); public float Alertness; public string State="PEACE";
    public int? TimerId; private int _recordControl;

    public BattleDetectionComponent(float interval=1000,int recLen=5,float thresh=1f,float battle=2f,float peace=1f,float max=5f)
    { IntervalMs=interval; RecordLength=recLen; DamageRateThreshold=thresh; AlertBattle=battle; AlertPeace=peace; AlertMax=max; }

    public string GetState()=>State;
    public void SetState(string s, System.Action<string>? broadcast=null){ if(s==State) return; State=s; broadcast?.Invoke(s); }

    public void TimerHandler(System.Action<int>? cancel=null, System.Action<string>? onState=null)
    {
        DamageRecord.Insert(0,Damage); if(DamageRecord.Count>RecordLength) DamageRecord.RemoveAt(DamageRecord.Count-1);
        Damage=0;
        if(Alertness==0 || _recordControl++==RecordLength-1)
        {
            float sum=0; foreach(var v in DamageRecord) sum+=v;
            float rate=sum/(RecordLength*IntervalMs);
            if(rate>DamageRateThreshold) Alertness=Math.Min(AlertMax,Alertness+1);
            else Alertness=Math.Max(0,Alertness-1);
            if(Alertness>=AlertBattle) SetState("BATTLE",onState);
            else if(Alertness<=AlertPeace) SetState("PEACE",onState);
        }
        if(_recordControl>RecordLength-1) _recordControl=0;
    }
    public void OnAttacked(int attackerOwner,int targetOwner,int playerId,float damage, System.Func<float,float,int>? setInterval=null)
    {
        if(attackerOwner!=playerId) return;
        if(targetOwner<=0||targetOwner==playerId) return;
        if(TimerId==null) TimerId=setInterval?.Invoke(0,IntervalMs);
        Damage+=damage;
    }
}
