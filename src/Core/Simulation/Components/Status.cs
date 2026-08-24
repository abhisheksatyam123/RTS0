/// Ported from mods/public/simulation/components/StatusBars.js + StatusEffectsReceiver.js + Fogging.js + Visibility.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors StatusBars.js — overlay bar generation state.</summary>
public sealed class StatusBarsComponent
{
    public static readonly string[] Sprites=new[]{"ExperienceBar","PackBar","UpgradeBar","ResourceSupplyBar","CaptureBar","HealthBar","AuraIcons","RankIcon"};
    public float BarWidth=1f, BarHeight=0.2f, HeightOffset=1f;
    public bool Enabled, ShowRank, ShowExperience;
    public bool UsedPlayerColors;
    public Dictionary<int,List<string>> AuraSources=new();

    public void SetEnabled(bool e,bool rank,bool exp, System.Action? regen=null)
    { if(e==Enabled&&rank==ShowRank&&exp==ShowExperience) return; Enabled=e; ShowRank=rank; ShowExperience=exp; regen?.Invoke(); }
    public void AddAuraSource(int src,string aura){ if(!AuraSources.TryGetValue(src,out var l)) AuraSources[src]=l=new(); l.Add(aura); }
    public void RemoveAuraSource(int src,string aura){ if(AuraSources.TryGetValue(src,out var l)) l.Remove(aura); }

    // Simplified regen — caller draws via overlay callbacks
    public void Regenerate(System.Action<string,float,float>? addBar=null, System.Func<int,float>? hpFrac=null, System.Func<int,float>? supplyFrac=null, System.Func<int,List<float>>? capture=null)
    {
        // stub: real drawing done by OverlayRenderer; here we just mark usedPlayerColors
        if(capture!=null) UsedPlayerColors=true;
    }
}

/// <summary>Mirrors StatusEffectsReceiver.js — timed modifiers/DoT.</summary>
public sealed class StatusEffectsReceiverComponent
{
    public const int DefaultIntervalMs=1000;
    public sealed class Effect
    {
        public string BaseCode=""; public float Duration, Interval; public string Stackability="Replace";
        public Dictionary<string,string> Modifiers=new(); public float TimeElapsed; public bool FirstTime=true;
        public int? TimerId; public (int ent,int owner) Source; public float Damage, Capture;
        public bool Timed => Duration>0 || Interval>0;
    }
    public Dictionary<string,Effect> Active=new();
    public IReadOnlyDictionary<string,Effect> GetActive()=>Active;

    public void ApplyStatus(Dictionary<string,Effect> data,int attacker,int owner)
    { foreach(var kv in data) AddStatus(kv.Key,kv.Value,attacker,owner); }

    public void AddStatus(string code, Effect data,int attacker=-1,int owner=-1, System.Action<string,Dictionary<string,string>>? addMods=null, System.Func<float,int>? setTimer=null)
    {
        string key=code;
        if(Active.TryGetValue(key,out var ex))
        {
            if(data.Stackability=="Ignore") return;
            if(data.Stackability=="Extend"){ ex.Duration+=data.Duration; return; }
            if(data.Stackability=="Replace") RemoveStatus(key);
            else if(data.Stackability=="Stack"){ int i=0; string t; do{ t=code+"_"+i++; }while(Active.ContainsKey(t)); key=t; }
        }
        var eff=new Effect{ BaseCode=code, Duration=data.Duration, Interval=data.Interval, Stackability=data.Stackability, Damage=data.Damage, Capture=data.Capture, Modifiers=new(data.Modifiers), Source=(attacker,owner) };
        if(eff.Modifiers.Count>0) addMods?.Invoke(key,eff.Modifiers);
        Active[key]=eff;
        if(!eff.Timed) return;
        if(eff.Interval==0) eff.Interval=DefaultIntervalMs;
        eff.TimerId=setTimer?.Invoke(eff.Interval);
    }
    public void RemoveStatus(string code, System.Action<string>? remMods=null, System.Action<int>? cancel=null)
    {
        if(!Active.TryGetValue(code,out var e)) return;
        if(e.Modifiers.Count>0) remMods?.Invoke(code);
        if(e.TimerId.HasValue) cancel?.Invoke(e.TimerId.Value);
        Active.Remove(code);
    }
    public void ExecuteEffect(string code,int lateness, System.Action<Effect>? doAttack=null, System.Action<int>? cancel=null)
    {
        if(!Active.TryGetValue(code,out var e)) return;
        if(e.Damage!=0||e.Capture!=0) doAttack?.Invoke(e);
        if(e.Duration==0) return;
        e.TimeElapsed+= e.FirstTime? lateness : e.Interval+lateness;
        e.FirstTime=false;
        if(e.TimeElapsed>=e.Duration) RemoveStatus(code,null,cancel);
    }
}

/// <summary>Mirrors Fogging.js — mirage per player.</summary>
public sealed class FoggingComponent
{
    public bool Activated; public List<int> Mirages=new(); public List<bool> Miraged=new(), Seen=new();
    public FoggingComponent(int numPlayers=8)
    { for(int i=0;i<numPlayers;i++){ Mirages.Add(-1); Miraged.Add(false); Seen.Add(false); } }
    public bool IsActivated()=>Activated;
    public void Activate(System.Action<int>? loadForPlayer=null)
    {
        bool first=!Activated; Activated=true;
        if(first) for(int p=0;p<Seen.Count;p++) if(Seen[p]) loadForPlayer?.Invoke(p);
    }
    public void LoadMirage(int player, System.Func<int,int>? addMirage=null, System.Action<int,int>? onLoaded=null)
    {
        if(!Activated) return; Miraged[player]=true;
        if(Mirages[player]==-1) Mirages[player]=addMirage?.Invoke(player)??-1;
        onLoaded?.Invoke(player,Mirages[player]);
    }
    public bool IsMiraged(int p)=> p>=0&&p<Miraged.Count&&Miraged[p];
    public int GetMirage(int p)=> p>=0&&p<Mirages.Count?Mirages[p]:-1;
    public bool WasSeen(int p)=> p>=0&&p<Seen.Count&&Seen[p];
    public void OnVisibilityChanged(int player,int vis) // 0 hidden 1 fogged 2 visible
    { if(vis==2){ Miraged[player]=false; Seen[player]=true; } else if(vis==1&&Activated) LoadMirage(player); }
}

/// <summary>Mirrors Visibility.js — scripted visibility flags.</summary>
public sealed class VisibilityComponent
{
    public bool RetainInFog, AlwaysVisible, IsCorpse, IsPreview;
    public bool Activated;
    public VisibilityComponent(bool retain=false,bool always=false,bool corpse=false,bool preview=false)
    { RetainInFog=retain; AlwaysVisible=always; IsCorpse=corpse; IsPreview=preview; if(preview||corpse) Activated=true; }
    public void SetActivated(bool v)=>Activated=v;
    public bool IsActivated()=>Activated;
    public int GetVisibility(int player,bool isVisible,bool isExplored,int owner)
    {
        if(IsPreview){ if(owner==player&&isExplored) return isVisible?2:1; return 0; }
        if(IsCorpse){ if(owner==player&&isExplored) return isVisible?2:1; return isVisible?2:0; }
        return 2;
    }
    public bool GetRetainInFog()=>RetainInFog;
    public bool GetAlwaysVisible()=>AlwaysVisible;
}
