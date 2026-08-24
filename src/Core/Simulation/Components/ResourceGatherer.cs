/* Ported from binaries/data/mods/public/simulation/components/ResourceGatherer.js — 0 A.D. */

using System.Collections.Generic;

namespace Rts0.Core;

public sealed class ResourceGathererComponent
{
    public float MaxDistance = 2.0f;
    public float BaseSpeed = 1.0f;
    public Dictionary<string, float> Rates = new();
    public Dictionary<string, float> Capacities = new();
    public Dictionary<string, int> Carrying = new();
    public (string generic, string specific)? LastCarriedType;

    public ResourceGathererComponent()
    {
        Rates["wood"] = 2; Capacities["wood"] = 10;
        Rates["food"] = 1; Capacities["food"] = 10;
        Rates["metal"] = 3; Capacities["metal"] = 10;
        Rates["stone"] = 3; Capacities["stone"] = 10;
    }

    public float GetCapacity(string generic) => Capacities.TryGetValue(generic, out var v) ? v : 0;

    public bool CanCarryMore(string generic)
    {
        int cur = Carrying.TryGetValue(generic, out var c) ? c : 0;
        return cur < GetCapacity(generic);
    }

    public bool CanGather(string generic) => Rates.ContainsKey(generic);

    public int GatherTick(string generic, int amount = 1)
    {
        if (!CanCarryMore(generic)) return 0;
        int cap = (int)GetCapacity(generic);
        int cur = Carrying.TryGetValue(generic, out var c) ? c : 0;
        int take = System.Math.Min(amount, cap - cur);
        Carrying[generic] = cur + take;
        LastCarriedType = (generic, generic);
        return take;
    }

    public Dictionary<string,int> DropAll()
    {
        var ret = new Dictionary<string,int>(Carrying);
        Carrying.Clear();
        return ret;
    }

    public List<(string type,int amount,int max)> GetCarryingStatus()
    {
        var ret = new List<(string,int,int)>();
        foreach (var kv in Carrying) ret.Add((kv.Key, kv.Value, (int)GetCapacity(kv.Key)));
        return ret;
    }
}
