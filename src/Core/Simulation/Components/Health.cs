/* Ported from binaries/data/mods/public/simulation/components/Health.js + CCmpHealth — 0 A.D. */

using System;

namespace Rts0.Core;

/// <summary> Mirrors Health.js schema: Max, RegenRate, DeathType. Deterministic. </summary>
public sealed class HealthComponent
{
    public int MaxHitpoints { get; private set; }
    public int Hitpoints { get; private set; }
    public float RegenRate { get; private set; }
    public float IdleRegenRate { get; private set; }
    public string DeathType { get; private set; } = "corpse";

    public bool IsDead => Hitpoints == 0;
    public bool IsInjured => Hitpoints > 0 && Hitpoints < MaxHitpoints;

    public HealthComponent(int max = 100, float regen = 0, float idleRegen = 0)
    {
        MaxHitpoints = max;
        Hitpoints = max;
        RegenRate = regen;
        IdleRegenRate = idleRegen;
    }

    public void Init(int max, int? initial = null, float regen = 0, float idleRegen = 0)
    {
        MaxHitpoints = max;
        Hitpoints = initial ?? max;
        if (Hitpoints == 0) Hitpoints = max;
        RegenRate = regen;
        IdleRegenRate = idleRegen;
    }

    public void SetHitpoints(int value)
    {
        if (IsDead) return;
        Hitpoints = Math.Clamp(value, 1, MaxHitpoints);
    }

    public void TakeDamage(int amount, string type = "Hack")
    {
        if (IsDead) return;
        Hitpoints = Math.Max(0, Hitpoints - amount);
    }

    public void Heal(int amount)
    {
        if (IsDead || Hitpoints == MaxHitpoints) return;
        Hitpoints = Math.Min(MaxHitpoints, Hitpoints + amount);
    }

    public void RegenTick(float dt, bool isIdle)
    {
        if (IsDead || IsInjured == false) return;
        float rate = isIdle ? IdleRegenRate : RegenRate;
        if (rate <= 0) return;
        Hitpoints = Math.Min(MaxHitpoints, Hitpoints + (int)(rate * dt));
    }
}
