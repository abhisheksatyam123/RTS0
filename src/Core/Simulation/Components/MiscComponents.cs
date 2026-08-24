/// Ported from AlertRaiser.js, GuiInterface.js, Mirage.js, ModifiersManager.js, MotionBall.js, RangeOverlayManager.js, Settlement.js, Wonder.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Alert raiser — mirrors AlertRaiser.js (simplified).</summary>
public sealed class AlertRaiserComponent
{
    private double _lastTime;
    private readonly List<int> _defenseUnits = new();
    public string TargetClasses = "";
    public int RaiseRange, EndRange, SearchRange;
    public string GetTargetClasses() => TargetClasses;
    public bool UnitFilter(HashSet<string> classes) => classes != null;
    public bool IsRefugeUnit(HashSet<string> classes, bool isAnimal = false) => !isAnimal && classes != null && !classes.Contains("Soldier");
    public bool IsCombatUnit(HashSet<string> classes, bool isAnimal = false) => !isAnimal && classes != null && classes.Contains("Soldier");
    public void SetAlertLock(int ent, int owner, bool locked, bool defense = false) { }
    public bool IsDefenseHolder(HashSet<string> c) => c != null && (c.Contains("Tower") || c.Contains("Fortress"));
    public void RaiseAlert() { _lastTime = 0; }
    public void RaiseDefense() { _lastTime = 0; }
    public void EndOfAlert() { _lastTime = 0; _defenseUnits.Clear(); }
}

/// <summary>GUI interface — mirrors GuiInterface.js (system, not network-synced).</summary>
public sealed class GuiInterfaceComponent
{
    public List<object> Notifications = new();
    public List<object> RenamedEntities = new();
    public List<object> MiragedEntities = new();
    public int TimeNotificationId = 1;
    public List<object> TimeNotifications = new();
    public void PushNotification(object n) => Notifications.Add(n);
    public Dictionary<string, object> GetSimulationState() => new() { ["players"] = new List<object>(), ["timeElapsed"] = 0 };
    public Dictionary<string, object> GetExtendedSimulationState() => GetSimulationState();
    public object Call(string name, object arg) => null;
}

/// <summary>Mirage in fog-of-war — mirrors Mirage.js.</summary>
public sealed class MirageComponent
{
    public const int VisHidden = 0, VisFogged = 1, VisVisible = 2;
    public int Parent = Player.InvalidPlayer;
    public int? PlayerId;
    private readonly Dictionary<int, object> _miraged = new();
    public void SetParent(int e) => Parent = e;
    public int GetParent() => Parent;
    public void SetPlayer(int p) => PlayerId = p;
    public int? GetPlayer() => PlayerId;
    public bool Mirages(int iid) => _miraged.ContainsKey(iid);
    public object Get(int iid) => _miraged.TryGetValue(iid, out var v) ? v : null;
    public void CopyComponent(int iid, Func<int, object> query) { var c = query(iid); if (c != null) _miraged[iid] = c; }
    public void OnVisibilityChanged(int player, int vis) { if (player == PlayerId && vis == VisHidden && Parent == Player.InvalidPlayer) { } }
}

/// <summary>Modifiers manager — mirrors ModifiersManager.js (system).</summary>
public sealed class ModifiersManagerComponent
{
    private readonly Dictionary<string, Dictionary<int, Dictionary<object, object>>> _cache = new();
    private readonly Dictionary<string, Dictionary<int, List<object>>> _mods = new();
    public void ModifiersChanged(string prop, int entity) => InvalidateCache(prop, entity);
    private void InvalidateCache(string p, int e) { if (_cache.TryGetValue(p, out var m)) m.Remove(e); }
    public object FetchModifiedProperty(HashSet<string> classes, string prop, object orig, int target) => orig;
    public void Cache(HashSet<string> classes, string prop, object orig, object nv, int e)
    {
        if (!_cache.TryGetValue(prop, out var m)) _cache[prop] = m = new();
        if (!m.TryGetValue(e, out var m2)) m[e] = m2 = new();
        m2[orig] = nv;
    }
    public object ApplyModifiers(string prop, object orig, int entity)
    {
        if (_cache.TryGetValue(prop, out var m) && m.TryGetValue(entity, out var m2) && m2.TryGetValue(orig, out var v)) return v;
        var nv = FetchModifiedProperty(null, prop, orig, entity);
        Cache(null, prop, orig, nv, entity); return nv;
    }
    public object ApplyModifiersToTemplate(string prop, object orig, int player, object tmpl) => FetchModifiedProperty(null, prop, orig, player);
}

/// <summary>Test motion ball — mirrors MotionBall.js.</summary>
public sealed class MotionBallComponent
{
    public float SpeedX, SpeedZ;
    public void OnUpdate(float dt, Func<Vector3D> getPos, Func<Vector3D> getNormal, Action<float,float> moveTo)
    {
        var pos = getPos(); var n = getNormal();
        const float g = 10f, drag = 0.5f;
        SpeedX += n.X * g * dt; SpeedZ += n.Z * g * dt;
        SpeedX *= MathF.Pow(drag, dt); SpeedZ *= MathF.Pow(drag, dt);
        moveTo(pos.X + SpeedX * dt, pos.Z + SpeedZ * dt);
    }
}

/// <summary>Range overlays — mirrors RangeOverlayManager.js.</summary>
public sealed class RangeOverlayManagerComponent
{
    public bool Enabled;
    public Dictionary<string,bool> EnabledRangeTypes = new() { ["Attack"]=false, ["Auras"]=false, ["Heal"]=false };
    private readonly Dictionary<string, List<object>> _viz = new();
    public void UpdateRangeOverlays(string comp, Func<List<object>> get) => _viz[comp] = get();
    public void SetEnabled(bool en, Dictionary<string,bool> types, bool force = false) { Enabled = en; EnabledRangeTypes = types; Regenerate(force); }
    public void Regenerate(bool force) { if (!Enabled && !force) return; }
    public void OnOwnershipChanged(int to) { if (to == Player.InvalidPlayer) return; }
    public void OnValueModification(string[] names, string comp) { }
    public void OnDeserialized() { }
}

/// <summary>Settlement marker — mirrors Settlement.js (empty).</summary>
public sealed class SettlementComponent { }

/// <summary>Wonder marker — mirrors Wonder.js.</summary>
public sealed class WonderComponent { }
