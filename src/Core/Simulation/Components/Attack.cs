/* Ported from binaries/data/mods/public/simulation/components/Attack.js — 0 A.D. */

using System.Collections.Generic;

namespace Rts0.Core;

public enum AttackType { Melee, Ranged, Capture, Slaughter }

public sealed class AttackComponent
{
    public sealed class AttackSpec
    {
        public string Name;
        public AttackType Type;
        public float MaxRange;
        public float MinRange;
        public float RepeatTimeMs;
        public Dictionary<string, float> Damage = new();
        public HashSet<string> PreferredClasses = new();
        public HashSet<string> RestrictedClasses = new();
    }

    private readonly Dictionary<AttackType, AttackSpec> _attacks = new();

    public void AddSpec(AttackSpec spec) => _attacks[spec.Type] = spec;

    public AttackSpec GetBestAttackAgainst(HashSet<string> targetClasses, bool allowCapture = false)
    {
        // Mirrors Attack.js GetBestAttackAgainst — picks highest damage valid attack
        AttackSpec best = null;
        float bestScore = -1;
        foreach (var kv in _attacks)
        {
            if (kv.Key == AttackType.Capture && !allowCapture) continue;
            if (IsRestricted(kv.Value, targetClasses)) continue;
            float score = 0;
            foreach (var d in kv.Value.Damage.Values) score += d;
            // Preferred bonus
            foreach (var c in kv.Value.PreferredClasses) if (targetClasses.Contains(c)) score *= 1.5f;
            if (score > bestScore) { bestScore = score; best = kv.Value; }
        }
        return best;
    }

    public bool CanAttack(string targetClass) => true; // simplified

    private static bool IsRestricted(AttackSpec spec, HashSet<string> targetClasses)
    {
        foreach (var c in spec.RestrictedClasses) if (targetClasses.Contains(c)) return true;
        return false;
    }

    public static AttackComponent DefaultMelee(float range = 1.5f, float dmg = 12)
    {
        var c = new AttackComponent();
        c.AddSpec(new AttackSpec { Name = "Melee", Type = AttackType.Melee, MaxRange = range, MinRange = 0, RepeatTimeMs = 1000, Damage = new() { ["Hack"] = dmg } });
        return c;
    }
}
