/// Ported from AttackDetection.js, DeathDamage.js, DelayedDamage.js, Resistance.js, FormationAttack.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Attack detection with suppression — mirrors AttackDetection.js.</summary>
public sealed class AttackDetectionComponent
{
    public float SuppressionTime = 8000;
    public float SuppressionRangeSq = 400;
    public float SuppressionTransferRangeSq = 100;
    public sealed class AttackEvent
    {
        public int Attacker; public int Target; public Vector3D Position; public double Time; public bool DomesticAnimal;
    }
    private readonly List<AttackEvent> _suppressed = new();
    public IReadOnlyList<AttackEvent> Suppressed => _suppressed;
    public void ActivateTimer() { }
    public void AddSuppression(AttackEvent e) { _suppressed.Add(e); ActivateTimer(); }
    public void UpdateSuppressionEvent(int idx, AttackEvent e) { _suppressed[idx] = e; ActivateTimer(); }
    public void OnGlobalAttacked(int target, int attacker, string type, int attackerOwner) => AttackAlert(target, attacker, type, attackerOwner);
    public void AttackAlert(int target, int attacker, string type, int attackerOwner)
    {
        var ev = new AttackEvent { Attacker = attacker, Target = target, Position = Vector3D.Zero, Time = 0, DomesticAnimal = false };
        for (int i = 0; i < _suppressed.Count; i++)
        {
            var el = _suppressed[i];
            float dx = ev.Position.X - el.Position.X, dz = ev.Position.Z - el.Position.Z;
            float dist = dx * dx + dz * dz;
            if (dist >= SuppressionRangeSq) continue;
            bool inc = el.DomesticAnimal && !ev.DomesticAnimal;
            bool dec = !el.DomesticAnimal && ev.DomesticAnimal;
            if (inc || (!dec && dist < SuppressionTransferRangeSq)) UpdateSuppressionEvent(i, ev);
            if (inc) break;
            return;
        }
        if (_suppressed.Count == 0 || !_suppressed[_suppressed.Count - 1].DomesticAnimal || ev.DomesticAnimal == false)
            AddSuppression(ev);
    }
    public float GetSuppressionTime() => SuppressionTime;
    public void HandleTimeout(double now)
    {
        for (int i = 0; i < _suppressed.Count; i++)
            if (now - _suppressed[i].Time >= SuppressionTime) { _suppressed.RemoveAt(i); return; }
    }
    public IReadOnlyList<AttackEvent> GetIncomingAttacks() => _suppressed;
}

/// <summary>Death splash damage — mirrors DeathDamage.js.</summary>
public sealed class DeathDamageComponent
{
    public string Shape = "Circular";
    public float Range = 20f;
    public bool FriendlyFire;
    public Dictionary<string, float> Damage = new();
    public Dictionary<string, object> GetDeathDamageEffects() => new() { ["Damage"] = Damage };
    public void CauseDeathDamage(int entity, Vector3D pos, int owner) { /* area damage stub */ }
}

/// <summary>Delayed projectile damage — mirrors DelayedDamage.js (system).</summary>
public sealed class DelayedDamageComponent
{
    public const float MissileHitRadius = 2f;
    public sealed class HitData
    {
        public string Type = "Ranged";
        public Dictionary<string, object> AttackData = new();
        public int Target; public int Attacker; public int AttackerOwner;
        public Vector3D Position; public int ProjectileId; public Vector3D Direction;
        public string AttackImpactSound = ""; public bool FriendlyFire;
        public SplashData Splash;
    }
    public sealed class SplashData { public bool FriendlyFire; public float Radius; public string Shape = "Circular"; public Dictionary<string, object> AttackData = new(); }
    public void Hit(HitData data, float lateness)
    {
        if (data.Position.Equals(default(Vector3D)) && data.Position.IsZero()) return;
        // splash + direct handled via AttackHelper stub
    }
}

/// <summary>Resistance — mirrors Resistance.js.</summary>
public sealed class ResistanceComponent
{
    private bool _invuln;
    private readonly HashSet<int> _attackers = new();
    public Dictionary<string, Dictionary<string, float>> TemplateDamage = new();
    public bool IsInvulnerable() => _invuln;
    public bool AddAttacker(int a) => _attackers.Add(a);
    public bool RemoveAttacker(int a) => _attackers.Remove(a);
    public void SetInvulnerability(bool v) => _invuln = v;
    public Dictionary<string, object> GetEffectiveResistanceAgainst(string effect)
    {
        var ret = new Dictionary<string, object>();
        if (TemplateDamage.TryGetValue(effect, out var d)) ret[effect] = d;
        return ret;
    }
    public Dictionary<string, object> GetFullResistance() => new();
    public Dictionary<string, object> GetResistanceOfForm(string form) => new();
    public void OnOwnershipChanged(int from, int to) { if (to == Player.InvalidPlayer) _attackers.Clear(); }
    public ResistanceMirage Mirage() { var m = new ResistanceMirage(); m.Init(this); return m; }
}
public sealed class ResistanceMirage
{
    public bool Invulnerable; public bool IsFoundation; public Dictionary<string, object> Resist = new();
    public HashSet<int> Attackers = new();
    public void Init(ResistanceComponent c) { Invulnerable = c.IsInvulnerable(); Resist = c.GetFullResistance(); }
    public bool IsInvulnerable() => Invulnerable;
    public bool AddAttacker(int a) => Attackers.Add(a);
    public bool RemoveAttacker(int a) => Attackers.Remove(a);
    public Dictionary<string, object> GetEffectiveResistanceAgainst(string f) => new();
    public Dictionary<string, object> GetResistanceOfForm(string f) => Resist.TryGetValue(f, out var v) ? (Dictionary<string, object>)v : new();
    public Dictionary<string, object> GetFullResistance() => Resist;
}

/// <summary>Formation attack — mirrors FormationAttack.js.</summary>
public sealed class FormationAttackComponent
{
    public bool CanAttackAsFormationFlag;
    public bool CanAttackAsFormation() => CanAttackAsFormationFlag;
    public string[] GetAttackTypes() => Array.Empty<string>();
    public (float min, float max) GetRange(int[] members, Func<int, (float min, float max)> getRange)
    {
        float min = 0, max = CanAttackAsFormationFlag ? -1 : 0;
        foreach (var m in members)
        {
            var r = getRange(m);
            if (CanAttackAsFormationFlag) { if (r.max < max || max < 0) max = r.max; }
            else { if (r.max > max) max = r.max; }
            if (r.min < min) min = r.min;
        }
        return (min, max);
    }
}
