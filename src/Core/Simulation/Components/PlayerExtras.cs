/// Ported from PlayerManager.js, SkirmishReplacer.js, ValueModificationManager.js, VisionSharing.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Player manager — mirrors PlayerManager.js (system).</summary>
public sealed class PlayerManagerComponent
{
    private readonly List<int> _players = new();
    public int AddPlayer(int entity, int id = -1)
    {
        if (id < 0) id = _players.Count;
        while (_players.Count <= id) _players.Add(Player.InvalidPlayer);
        _players[id] = entity; return id;
    }
    public void ReplacePlayerTemplate(int id, int newEnt, Action<int,int> reassign)
    {
        int old = _players[id]; _players[id] = newEnt;
        reassign(old, newEnt);
    }
    public int GetPlayerByID(int id) => (id >= 0 && id < _players.Count) ? _players[id] : Player.InvalidPlayer;
    public int GetNumPlayers() => _players.Count;
    public int[] GetAllPlayers() { var a = new int[_players.Count]; for (int i = 0; i < a.Length; i++) a[i] = i; return a; }
    public int[] GetNonGaiaPlayers() { var l = new List<int>(); for (int i = 1; i < _players.Count; i++) l.Add(i); return l.ToArray(); }
    public int[] GetActivePlayers(Func<int,bool> isActive)
    {
        var l = new List<int>();
        foreach (var p in GetNonGaiaPlayers()) if (isActive(p)) l.Add(p);
        return l.ToArray();
    }
    public void RemoveLastPlayer() { if (_players.Count > 0) _players.RemoveAt(_players.Count - 1); }
}

/// <summary>Skirmish replacer — mirrors SkirmishReplacer.js.</summary>
public sealed class SkirmishReplacerComponent
{
    public string GeneralTemplate = "";
    public void OnOwnershipChanged(int to) { if (to == 0) { /* warn */ } }
    public int ReplaceEntities(string civ, string templateName, Func<string, string> getReplacement, Func<string,int> addEntity, Action<Vector2D,float,int> place)
    {
        if (string.IsNullOrEmpty(civ) || civ == "gaia") return Player.InvalidPlayer;
        string repl = getReplacement(templateName);
        if (string.IsNullOrEmpty(repl)) repl = GeneralTemplate;
        if (string.IsNullOrEmpty(repl)) return Player.InvalidPlayer;
        repl = repl.Replace("{civ}", civ);
        int ent = addEntity(repl);
        return ent;
    }
    public void OnSkirmishReplace() { }
    public void OnUpdate() { }
}

/// <summary>Value modification manager — mirrors ValueModificationManager.js (system).</summary>
public sealed class ValueModificationManagerComponent
{
    public object ApplyModifications(string name, object cur, int entity) => cur;
}

/// <summary>Vision sharing — mirrors VisionSharing.js.</summary>
public sealed class VisionSharingComponent
{
    public bool Bribable;
    private bool _activated;
    private HashSet<int> _shared;
    private int _spyId;
    private Dictionary<int,int> _spies;
    public bool IsBribable() => Bribable;
    public void Activate(int owner)
    {
        if (_activated || owner <= 0) return;
        _shared = new HashSet<int> { owner }; _activated = true;
    }
    public void CheckVisionSharings(int owner, IEnumerable<int> garrisoned, Func<int,int> getOwner)
    {
        var shared = new HashSet<int>();
        if (owner > 0) shared.Add(owner);
        if (garrisoned != null) foreach (var e in garrisoned) { int o = getOwner(e); if (o > 0 && o != owner) { shared.Add(o); Activate(owner); } }
        if (_spies != null) foreach (var kv in _spies) if (kv.Value > 0) shared.Add(kv.Value);
        if (!_activated) return;
        _shared = shared;
    }
    public void OnGarrisonedUnitsChanged(int owner, IEnumerable<int> ents, Func<int,int> go) => CheckVisionSharings(owner, ents, go);
    public void OnOwnershipChanged() { if (_activated) { } }
    public int AddSpy(int player, float duration, int owner, Func<bool> canBribe)
    {
        if (!Bribable || owner == player || player <= 0) return 0;
        if (!canBribe()) return 0;
        _spies ??= new(); _spies[++_spyId] = player; Activate(owner);
        return _spyId;
    }
    public void RemoveSpy(int id) { _spies?.Remove(id); }
    public bool ShareVisionWith(int player, int owner) => _activated ? _shared != null && _shared.Contains(player) : owner == player;
}
