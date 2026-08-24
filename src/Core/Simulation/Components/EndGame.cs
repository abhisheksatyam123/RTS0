/// Ported from mods/public/simulation/components/EndGameManager.js + TerritoryDecay.js + TerritoryDecayManager.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

/// <summary>Mirrors EndGameManager.js — victory conditions, allied victory check.</summary>
public sealed class EndGameManagerComponent
{
    public Dictionary<string,object> GameSettings=new();
    public List<string> VictoryConditions=new();
    public bool AlliedVictory=true;
    public bool SkipCheck=true;
    public bool EndlessGame;
    public string? LastManStandingMsg;

    public Dictionary<string,object> GetGameSettings()=>GameSettings;
    public List<string> GetVictoryConditions()=>VictoryConditions;
    public void SetGameSettings(Dictionary<string,object>? s, System.Action? broadcast=null)
    {
        GameSettings=s??new(); VictoryConditions=GameSettings.TryGetValue("victoryConditions",out var v)&&v is List<string> ls? ls : new();
        SkipCheck=false; EndlessGame=VictoryConditions.Count==0; broadcast?.Invoke();
    }
    public void SetAlliedVictory(bool v)=>AlliedVictory=v;
    public bool GetAlliedVictory()=>AlliedVictory;

    public void MarkPlayersAsWon(List<int> winners, System.Func<int,string> victoryStr, System.Func<int,string> defeatStr,
        System.Func<int,bool> isActive, System.Action<int,int?> setState, System.Action<string,List<int>> pushNotif)
    {
        SkipCheck=true;
        foreach(var p in winners) if(isActive(p)) setState(p,1);
        var defeated=new List<int>(); // find active not in winners
        // caller enumerates players; simplified: no defeated discovery here
        pushNotif(victoryStr(winners.Count),winners);
        if(defeated.Count>0) pushNotif(defeatStr(defeated.Count),defeated);
        SkipCheck=false;
    }
    public List<int>? AlliedVictoryCheck(List<int> activePlayers, System.Func<int,List<int>> mutualAllies, System.Action<string>? notify=null)
    {
        if(SkipCheck||EndlessGame) return null;
        if(activePlayers.Count==0) return null;
        // all remaining must be mutual allies
        for(int i=1;i<activePlayers.Count;i++) if(!mutualAllies(activePlayers[0]).Contains(activePlayers[i])) return null;
        if(AlliedVictory||activePlayers.Count==1) return activePlayers;
        LastManStandingMsg="Last remaining player wins.";
        notify?.Invoke(LastManStandingMsg);
        return null;
    }
}

/// <summary>Mirrors TerritoryDecay.js — decay when unconnected.</summary>
public sealed class TerritoryDecayComponent
{
    public float DecayRate=1f; public bool TerritoryOwnership; public bool Decaying;
    public List<float> ConnectedNeighbours=new();
    public HashSet<string> DecayTerritories=new(); // "neutral","enemy"

    public TerritoryDecayComponent(float rate=1f,string territory="enemy")
    { DecayRate=rate; TerritoryOwnership=!float.IsFinite(rate); foreach(var t in territory.Split(' ',System.StringSplitOptions.RemoveEmptyEntries)) DecayTerritories.Add(t); }

    public bool IsDecaying()=>Decaying;
    public float GetDecayRate()=>DecayRate;
    public List<float> GetConnectedNeighbours()=>ConnectedNeighbours;
    public bool HasTerritoryOwnership()=>TerritoryOwnership;

    public bool IsConnected(int owner,float x,float z, System.Func<float,float,int> getOwner, System.Func<float,float,bool> isConnected, System.Func<float,float,List<float>> getNeighbours, System.Func<int,bool> isMutualAlly, System.Action<float,float,bool>? setBlinking=null)
    {
        ConnectedNeighbours=new List<float>(new float[8]); // simplified size
        int tileOwner=getOwner(x,z);
        if(tileOwner==0) return owner==0 || !DecayTerritories.Contains("neutral");
        bool conn=isConnected(x,z);
        if(conn && !isMutualAlly(tileOwner)) return !DecayTerritories.Contains("enemy");
        if(conn) return true;
        if(owner!=tileOwner) return false;
        var neigh=getNeighbours(x,z);
        for(int i=1;i<neigh.Count;i++) if(neigh[i]>0 && isMutualAlly(i)){ setBlinking?.Invoke(x,z,false); return true; }
        setBlinking?.Invoke(x,z,true); return false;
    }
    public void UpdateDecayState(System.Func<bool> isConnected, System.Action<bool,float>? broadcast=null)
    {
        bool dec=!isConnected() && DecayRate>0;
        if(dec==Decaying) return; Decaying=dec; broadcast?.Invoke(dec,DecayRate);
    }
}

/// <summary>Mirrors TerritoryDecayManager.js — system registry.</summary>
public sealed class TerritoryDecayManagerComponent
{
    private readonly HashSet<int> _list=new();
    public IReadOnlyCollection<int> List=>_list;
    public void Add(int e)=>_list.Add(e);
    public void Remove(int e)=>_list.Remove(e);
    public void SetBlinkingEntities(System.Action<int> ensureConnected)
    { foreach(var e in _list) ensureConnected(e); }
}
