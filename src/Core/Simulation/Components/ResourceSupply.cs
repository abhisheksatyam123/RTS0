/* Ported from binaries/data/mods/public/simulation/components/ResourceSupply.js — 0 A.D. */

using System.Collections.Generic;

namespace Rts0.Core;

public sealed class ResourceSupplyComponent
{
    public string GenericType { get; private set; } // "wood", "food", "metal", "stone"
    public string SpecificType { get; private set; } // "tree", "ore" etc
    public int Amount { get; private set; }
    public int MaxAmount { get; private set; }
    public bool Infinite { get; private set; }

    public ResourceSupplyComponent(string generic, string specific, int amount, bool infinite = false)
    {
        GenericType = generic;
        SpecificType = specific;
        Amount = amount;
        MaxAmount = amount;
        Infinite = infinite;
    }

    public int Gather(int wanted)
    {
        if (Infinite) return wanted;
        int taken = System.Math.Min(wanted, Amount);
        Amount -= taken;
        return taken;
    }

    public bool IsDepleted => !Infinite && Amount <= 0;

    // Mirrors ResourceSupply.js GetKillBeforeGather
    public bool KillBeforeGather => GenericType == "food" && SpecificType == "meat";
}
