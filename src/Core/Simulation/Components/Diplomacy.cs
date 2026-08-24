/// Ported from mods/public/simulation/components/Diplomacy.js + CeasefireManager.js — 0 A.D. GPL-2.0
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

/// <summary>Mirrors Diplomacy.js — team, diplomacy array, shared LOS/dropsites.</summary>
public sealed class DiplomacyComponent
{
    public int Team = -1;
    public bool TeamLocked;
    public List<int> Diplomacy = new(); // -1 enemy, 0 neutral, 1 ally
    public bool SharedDropsites;
    public string SharedLosTech = "", SharedDropsitesTech="";

    public int GetTeam()=>Team;
    public bool IsTeamLocked()=>TeamLocked;
    public void LockTeam()=>TeamLocked=true;
    public void UnlockTeam()=>TeamLocked=false;
    public List<int> GetDiplomacy()=> new(Diplomacy);
    public void SetDiplomacy(List<int> d){ Diplomacy=new List<int>(d); }
    public void SetDiplomacyIndex(int idx,int val, System.Action<int,int>? broadcast=null)
    { if(idx<0||idx>=Diplomacy.Count) return; Diplomacy[idx]=val; broadcast?.Invoke(idx,val); }

    public void ChangeTeam(int team, int selfId, System.Func<int,DiplomacyComponent?> getDiplom, System.Action<int,int,int>? onTeamChanged=null)
    {
        if(TeamLocked||Team==team) return;
        int old=Team; Team=team;
        if(Team!=-1)
        {
            for(int i=0;i<Diplomacy.Count;i++)
            {
                var other=getDiplom(i);
                if(other==null||other.GetTeam()!=Team) continue;
                SetDiplomacyIndex(i,1,null); other.SetDiplomacyIndex(selfId,1,null);
            }
        }
        onTeamChanged?.Invoke(selfId,old,team);
    }
    public List<int> GetPlayersBy(System.Func<int,bool> pred)
    { var r=new List<int>(); for(int i=0;i<Diplomacy.Count;i++) if(pred(i)) r.Add(i); return r; }
    public void Ally(int id, System.Action<int,int>? bc=null)=>SetDiplomacyIndex(id,1,bc);
    public bool IsAlly(int id)=> id>=0&&id<Diplomacy.Count&&Diplomacy[id]>0;
    public List<int> GetAllies()=>GetPlayersBy(IsAlly);
    public bool IsMutualAlly(int id,int selfId, System.Func<int,DiplomacyComponent?> get){ var o=get(id); return o!=null&&IsAlly(id)&&o.IsAlly(selfId); }
    public List<int> GetMutualAllies(int selfId, System.Func<int,DiplomacyComponent?> get)
        => GetPlayersBy(i=>IsMutualAlly(i,selfId,get));
    public void SetEnemy(int id,System.Action<int,int>? bc=null)=>SetDiplomacyIndex(id,-1,bc);
    public bool IsEnemy(int id)=> id>=0&&id<Diplomacy.Count&&Diplomacy[id]<0;
    public List<int> GetEnemies()=>GetPlayersBy(IsEnemy);
    public void SetNeutral(int id,System.Action<int,int>? bc=null)=>SetDiplomacyIndex(id,0,bc);
    public bool IsNeutral(int id)=> id>=0&&id<Diplomacy.Count&&Diplomacy[id]==0;
    public bool HasSharedDropsites()=>SharedDropsites;
    public bool HasSharedLos(System.Func<string,bool> isResearched)=> isResearched(SharedLosTech);
}

/// <summary>Mirrors CeasefireManager.js — system singleton.</summary>
public sealed class CeasefireManagerComponent
{
    public bool IsActive;
    public float CeasefireTimeMs;
    public float StartedTimeMs;
    public List<List<int>> DiplomacyBefore=new();
    public int? StopTimer, CountdownTimer;
    public bool HasCountdown;

    public bool IsCeasefireActive()=>IsActive;
    public float GetStarted()=>StartedTimeMs;
    public float GetTime()=>CeasefireTimeMs;

    public void StartCeasefire(float durationMs,float nowMs, int numPlayers, System.Func<int,DiplomacyComponent?> getDiplom, System.Action<int,int>? setNeutral, System.Func<float,int>? setTimeout)
    {
        if(durationMs<=0) return;
        if(!IsActive)
        {
            DiplomacyBefore.Clear();
            for(int i=1;i<numPlayers;i++) DiplomacyBefore.Add(getDiplom(i)?.GetDiplomacy() ?? new List<int>());
            for(int i=1;i<numPlayers;i++) for(int j=1;j<numPlayers;j++) if(DiplomacyBefore[i-1].Count>j && DiplomacyBefore[i-1][j]<0) setNeutral?.Invoke(i,j);
        }
        IsActive=true; CeasefireTimeMs=durationMs; StartedTimeMs=nowMs;
        StopTimer=setTimeout?.Invoke(durationMs);
        if(durationMs>10000) CountdownTimer=setTimeout?.Invoke(durationMs-10000);
    }
    public void StopCeasefire(int numPlayers, System.Func<int,DiplomacyComponent?> getDiplom)
    {
        for(int i=1;i<numPlayers;i++) if(i-1<DiplomacyBefore.Count) getDiplom(i)?.SetDiplomacy(DiplomacyBefore[i-1]);
        IsActive=false; CeasefireTimeMs=0; StartedTimeMs=0; DiplomacyBefore.Clear();
        StopTimer=null; CountdownTimer=null;
    }
}
