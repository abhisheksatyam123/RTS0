/// Ported from /source/mods/public/simulation/components/Player.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Player.js state + resources. Called from GameSim for economy.</summary>
public sealed class PlayerComponent
{
    public const string StateActive = "active";
    public const string StateDefeated = "defeated";
    public const string StateWon = "won";

    public int PlayerId;
    public string State = StateActive;
    public int MaxPop = 300;
    public int PopUsed;
    public int PopBonuses;
    public bool TrainingBlocked;
    public float SpyCostMultiplier = 1f;
    public bool IsAI;
    public bool IsRemoved;
    public bool ControlAllUnits;

    public Dictionary<string, int> ResourceCount = new();
    public Dictionary<string, int> ResourceGatherers = new();
    public Dictionary<string, float> BarterBuy = new();
    public Dictionary<string, float> BarterSell = new();
    public List<string> Formations = new();
    public HashSet<string> DisabledTemplates = new();
    public HashSet<string> DisabledTechs = new();
    public List<int> PanelEntities = new();
    public List<int> BarterEntities = new();

    public PlayerComponent() : this(0, new[] { "food", "wood", "stone", "metal" }) { }
    public void Init(int id) { PlayerId = id; }
    public void Tick(float dt, System.Action<string,int> addGold) { /* ResourceTrickle handled in GameSim */ }

    public PlayerComponent(int id, IEnumerable<string> resCodes)
    {
        PlayerId = id;
        foreach (var r in resCodes)
        {
            ResourceCount[r] = 300;
            ResourceGatherers[r] = 0;
        }
        BarterBuy["food"] = BarterBuy["wood"] = BarterBuy["stone"] = BarterBuy["metal"] = 1f;
        BarterSell["food"] = BarterSell["wood"] = BarterSell["stone"] = BarterSell["metal"] = 1f;
    }

    public int PopulationLimit => System.Math.Min(MaxPop, PopBonuses);
    public bool IsActive => State == StateActive;

    public int TryReservePop(int n)
    {
        int free = PopulationLimit - PopUsed;
        if (n > free) return n - free;
        PopUsed += n; return 0;
    }
    public void AddPop(int n) => PopUsed += n;
    public void AddPopBonus(int n) => PopBonuses += n;

    public Dictionary<string,int>? GetNeeded(Dictionary<string,int> cost)
    {
        Dictionary<string,int>? need = null;
        foreach (var kv in cost)
            if (ResourceCount.GetValueOrDefault(kv.Key) < kv.Value)
                (need ??= new())[kv.Key] = kv.Value - ResourceCount.GetValueOrDefault(kv.Key);
        return need;
    }
    public bool TrySubtract(Dictionary<string,int> cost)
    {
        if (GetNeeded(cost) != null) return false;
        foreach (var kv in cost) ResourceCount[kv.Key] -= kv.Value;
        return true;
    }
    public void AddResources(Dictionary<string,int> r) { foreach (var kv in r) ResourceCount[kv.Key] = ResourceCount.GetValueOrDefault(kv.Key) + kv.Value; }
    public void AddResource(string t,int a) => ResourceCount[t] = ResourceCount.GetValueOrDefault(t) + a;

    public void SetState(string s)
    {
        if (!IsActive || s == StateActive) return;
        State = s;
    }
    public bool CanBarter() => BarterEntities.Count > 0;
}
