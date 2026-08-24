/// Ported from /source/mods/public/simulation/components/Population.js + PopulationCapManager.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Population bonus entity — mirrors Population.js.</summary>
public sealed class PopulationComponent
{
    public int Bonus;
    public PopulationComponent(int bonus=0)=>Bonus=bonus;
    public int GetPopBonus()=>Bonus;
    public void Recalculate(int rawBonus)=>Bonus=rawBonus;
}

/// <summary>System pop-cap manager — mirrors PopulationCapManager.js.</summary>
public sealed class PopulationCapManager
{
    public const string CapPlayer="player", CapTeam="team", CapWorld="world";
    public string CapType=CapPlayer;
    public int PopCap=300;
    public Dictionary<int,int> PerPlayerCaps=new();

    public void SetCapType(string t)=>CapType=t;
    public void SetCap(int c)=>PopCap=c;
    public void SetPerPlayerCaps(Dictionary<int,int> caps){ PerPlayerCaps=caps; CapType=CapPlayer; }

    public void InitializePlayerCaps(Dictionary<int,PlayerComponent> players)
    {
        foreach(var kv in players) kv.Value.MaxPop = PerPlayerCaps.TryGetValue(kv.Key,out var pc)? pc : PopCap;
    }
    public void InitializeTeamCaps(Dictionary<int,PlayerComponent> players, Dictionary<int,int> teamByPlayer)
    {
        var byTeam=new Dictionary<int,List<int>>();
        foreach(var kv in players){ int t=teamByPlayer.GetValueOrDefault(kv.Key,-1); if(!byTeam.ContainsKey(t)) byTeam[t]=new(); byTeam[t].Add(kv.Key); }
        foreach(var kv in byTeam)
        {
            int cap = kv.Key==-1 ? PopCap : PopCap / kv.Value.Count;
            foreach(var pid in kv.Value) if(players.TryGetValue(pid,out var p)) p.MaxPop=cap;
        }
    }
    public void RedistributeWorld(Dictionary<int,PlayerComponent> players)
    {
        if(players.Count==0) return;
        int cap=PopCap/players.Count;
        foreach(var kv in players) kv.Value.MaxPop=cap;
    }
    public void OnPlayerDefeated(int playerId, Dictionary<int,PlayerComponent> players, Dictionary<int,int> teamByPlayer)
    {
        if(CapType==CapTeam) { int t=teamByPlayer.GetValueOrDefault(playerId,-1); if(t!=-1) InitializeTeamCaps(players, teamByPlayer); }
        else if(CapType==CapWorld) RedistributeWorld(players);
    }
}
