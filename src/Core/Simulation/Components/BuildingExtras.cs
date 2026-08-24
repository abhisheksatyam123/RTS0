/// Ported from BuildRestrictions.js, AutoBuildable.js, AutoWorkers.js, EntityLimits.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Placement restrictions — mirrors BuildRestrictions.js.</summary>
public sealed class BuildRestrictionsComponent
{
    public string PlacementType = "land"; // land|shore|land-shore
    public string Territory = "own";
    public string Category = "Structure";
    public int? MatchLimit;
    public string FromClass = "";
    public int? MinDistance, MaxDistance;
    public string GetCategory() => Category;
    public string[] GetTerritories() => Territory.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
    public bool HasTerritory(string t) => Array.IndexOf(GetTerritories(), t) >= 0;
    public struct CheckResult { public bool Success; public string Message; public Dictionary<string, object> Params; }
    public CheckResult CheckPlacement(int owner, string los = "visible", bool obstructed = false, string terr = "own")
    {
        if (los == "hidden") return new() { Success = false, Message = "cannot be built in unexplored area" };
        if (obstructed) return new() { Success = false, Message = "cannot be built on another building" };
        if (!HasTerritory(terr)) return new() { Success = false, Message = $"cannot be built in {terr} territory" };
        if (PlacementType == "shore" && terr == "own") { /* shore check stub */ }
        return new() { Success = true, Message = "" };
    }
}

/// <summary>Auto-build — mirrors AutoBuildable.js.</summary>
public sealed class AutoBuildableComponent
{
    public float Rate;
    private int _timerId;
    public float GetRate() => Rate;
    public void UpdateRate(float newRate) { Rate = newRate; if (Rate > 0) StartTimer(); else CancelTimer(); }
    public void StartTimer() { if (_timerId != 0 || Rate <= 0) return; _timerId = 1; }
    public void CancelTimer() { if (_timerId == 0) return; _timerId = 0; }
    public void AutoBuild(Action<float> build) { if (Rate <= 0) { CancelTimer(); return; } build(Rate); }
    public void OnValueModification(string comp) { if (comp == "AutoBuildable") UpdateRate(Rate); }
    public void OnOwnershipChanged(int to) { if (to != Player.InvalidPlayer) UpdateRate(Rate); }
}

/// <summary>Auto workers level — mirrors AutoWorkers.js (0 off |1 work|2 town|3 war).</summary>
public sealed class AutoWorkersComponent
{
    public const int LevelOff = 0, LevelWork = 1, LevelTown = 2, LevelWar = 3;
    private int _level = LevelOff;
    public int ClampLevel(int n) => n != n ? LevelOff : Math.Clamp(n, LevelOff, LevelWar);
    public bool CanPlay(int playerId, bool isAi, string state) => !isAi && state == "active";
    public Dictionary<string, object> Serialize() => new() { ["level"] = _level };
    public void Deserialize(Dictionary<string, object> d)
    {
        if (d != null && d.TryGetValue("level", out var v)) _level = ClampLevel(Convert.ToInt32(v));
        else if (d != null && d.ContainsKey("enabled")) _level = LevelTown;
    }
    public bool IsEnabled() => _level > LevelOff;
    public int GetLevel() => _level;
    public bool SetLevel(int lvl, bool canPlay = true)
    {
        lvl = ClampLevel(lvl);
        if (lvl > LevelOff && !canPlay) return false;
        _level = lvl; return true;
    }
    public bool Enable() => SetLevel(LevelTown);
    public void Disable() => _level = LevelOff;
    public bool Cycle() { int n = (_level + 1) % (LevelWar + 1); if (n == LevelOff) { Disable(); return true; } return SetLevel(n); }
}

/// <summary>Entity limits — mirrors EntityLimits.js.</summary>
public sealed class EntityLimitsComponent
{
    public Dictionary<string, int> Limit = new();
    public Dictionary<string, int> Count = new();
    public Dictionary<string, Dictionary<string, int>> Changers = new();
    public Dictionary<string, Dictionary<string, string[]>> Removers = new();
    public Dictionary<string, int> ClassCount = new();
    public Dictionary<string, int> RemovedLimit = new();
    public Dictionary<string, int> MatchCount = new();
    public void ChangeCount(string cat, int v) { if (Count.ContainsKey(cat)) Count[cat] += v; }
    public void ChangeMatchCount(string tmpl, int v) { if (!MatchCount.ContainsKey(tmpl)) MatchCount[tmpl] = 0; MatchCount[tmpl] += v; }
    public IReadOnlyDictionary<string,int> GetLimits() => Limit;
    public IReadOnlyDictionary<string,int> GetCounts() => Count;
    public IReadOnlyDictionary<string,int> GetMatchCounts() => MatchCount;
    public IReadOnlyDictionary<string, Dictionary<string,int>> GetLimitChangers() => Changers;
    public void UpdateLimitsFromTech(string tech)
    {
        foreach (var kv in Removers) if (kv.Value.TryGetValue("RequiredTechs", out var arr))
            Removers[kv.Key]["RequiredTechs"] = Array.FindAll(arr, x => x != tech);
        UpdateLimitRemoval();
    }
    public void UpdateLimitRemoval()
    {
        foreach (var cat in new List<string>(Removers.Keys))
        {
            bool nolimit = true;
            if (Removers[cat].TryGetValue("RequiredTechs", out var t)) nolimit = t.Length == 0;
            if (nolimit && Removers[cat].TryGetValue("RequiredClasses", out var cls))
                foreach (var c in cls) nolimit &= ClassCount.TryGetValue(c, out var cnt) && cnt > 0;
            if (nolimit && Limit.ContainsKey(cat)) { RemovedLimit[cat] = Limit[cat]; Limit.Remove(cat); }
            else if (!nolimit && !Limit.ContainsKey(cat) && RemovedLimit.ContainsKey(cat)) Limit[cat] = RemovedLimit[cat];
        }
    }
    public bool AllowedToCreate(string type, string cat, int cnt, string tmpl, int? matchLimit)
    {
        if (Count.TryGetValue(cat, out var c) && Limit.TryGetValue(cat, out var lim) && c + cnt > lim) return false;
        if (tmpl != null && MatchCount.TryGetValue(tmpl, out var mc) && matchLimit.HasValue && mc + cnt > matchLimit.Value) return false;
        return true;
    }
    public bool AllowedToBuild(string cat) => AllowedToCreate("build", cat, 0, null, null);
    public bool AllowedToTrain(string cat, int cnt, string tmpl, int? ml) => AllowedToCreate("training", cat, cnt, tmpl, ml);
    public bool AllowedToReplace(string fromCat, string toCat, int d = 1) => AllowedToCreate("training", toCat, fromCat == toCat ? 0 : d, null, null);
    public void OnGlobalOwnershipChanged(int from, int to, int playerId, string cat, string[] classes)
    {
        int mod = to == playerId ? 1 : from == playerId ? -1 : 0; if (mod == 0) return;
        if (cat != null) ChangeCount(cat, mod);
        if (classes == null) return;
        var set = new HashSet<string>(classes);
        foreach (var kv in Changers) foreach (var ch in kv.Value)
            if (set.Contains(ch.Key)) { if (Limit.ContainsKey(kv.Key)) Limit[kv.Key] += mod * ch.Value; if (RemovedLimit.ContainsKey(kv.Key)) RemovedLimit[kv.Key] += mod * ch.Value; }
        foreach (var kv in Removers) if (kv.Value.TryGetValue("RequiredClasses", out var req)) foreach (var c in req) if (set.Contains(c))
            { if (!ClassCount.ContainsKey(c)) ClassCount[c]=0; ClassCount[c]+=mod; }
        UpdateLimitRemoval();
    }
}
