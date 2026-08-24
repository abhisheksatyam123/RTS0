/* Ported from source/simulation2/system/ComponentManager.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>Minimal component manager — mirrors CComponentManager (source/simulation2/system/ComponentManager.h/.cpp).</summary>
/// <remarks>Reference: source/simulation2/system/ComponentManager.h, ComponentManager.cpp (RegisterComponent, AddComponent, QueryInterface, BroadcastMessage)</remarks>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Optional message handler for components. Mirrors IComponent::HandleMessage.</summary>
public interface IMessageHandler
{
    void HandleMessage(CMessage msg, bool global);
}

public sealed class ComponentManager
{
    private readonly Dictionary<string, int> _nameToCid = new();
    private readonly Dictionary<int, string> _cidToName = new();
    private readonly Dictionary<int, int> _cidToIid = new();
    private readonly Dictionary<int, Func<uint, ParamNode, IComponent>> _factories = new();
    private readonly Dictionary<(uint, int), IComponent> _byInterface = new();
    private readonly Dictionary<(uint, int), IComponent> _byType = new();
    private readonly Dictionary<int, List<int>> _localSubs = new();
    private readonly Dictionary<int, List<int>> _globalSubs = new();
    private readonly Dictionary<uint, EntityComponentCache> _caches = new();
    private readonly List<uint> _destructionQueue = new();
    private int _nextCid = 1000;
    private uint _nextEntityId = Entity.SystemEntity + 1;
    private uint _nextLocalId = Entity.FirstLocal;
    private EntityHandle _systemHandle;

    public int RegisterComponent(string name, int iid, Func<uint, ParamNode, IComponent> factory)
    {
        if (_nameToCid.TryGetValue(name, out var existing)) return existing;
        int cid = _nextCid++;
        _nameToCid[name] = cid; _cidToName[cid] = name; _cidToIid[cid] = iid; _factories[cid] = factory;
        return cid;
    }
    public int LookupCID(string name) => _nameToCid.TryGetValue(name, out var id) ? id : -1;
    public string LookupComponentTypeName(int cid) => _cidToName.TryGetValue(cid, out var n) ? n : "";

    public void SubscribeToMessageType(int mtid, int cid)
    {
        if (!_localSubs.TryGetValue(mtid, out var l)) _localSubs[mtid] = l = new();
        if (!l.Contains(cid)) l.Add(cid);
    }
    public void SubscribeGloballyToMessageType(int mtid, int cid)
    {
        if (!_globalSubs.TryGetValue(mtid, out var l)) _globalSubs[mtid] = l = new();
        if (!l.Contains(cid)) l.Add(cid);
    }

    public EntityHandle GetSystemEntity() => _systemHandle;
    public void InitSystemEntity()
    {
        var cache = new EntityComponentCache(64);
        _caches[Entity.SystemEntity] = cache;
        _systemHandle = new EntityHandle(Entity.SystemEntity, cache);
    }
    public EntityHandle LookupEntityHandle(uint ent, bool allowCreate = false)
    {
        if (_caches.TryGetValue(ent, out var c)) return new EntityHandle(ent, c);
        if (!allowCreate) return new EntityHandle(ent, null);
        var nc = new EntityComponentCache(64); _caches[ent] = nc;
        return new EntityHandle(ent, nc);
    }
    public bool EntityExists(uint ent) => _caches.ContainsKey(ent);
    public uint AllocateNewEntity() => _nextEntityId++;
    public uint AllocateNewLocalEntity() => _nextLocalId++;
    public uint AllocateNewEntity(uint preferred) { if (preferred >= _nextEntityId) _nextEntityId = preferred + 1; return preferred; }

    public bool AddComponent(EntityHandle handle, int cid, ParamNode param)
    {
        if (!_factories.TryGetValue(cid, out var fac)) return false;
        int iid = _cidToIid[cid];
        if (_byInterface.ContainsKey((handle.Id, iid))) return false;
        var comp = fac(handle.Id, param);
        _byInterface[(handle.Id, iid)] = comp;
        _byType[(handle.Id, cid)] = comp;
        if (handle.Cache is EntityComponentCache cache && iid < cache.Interfaces.Length)
            cache.Interfaces[iid] = comp;
        return true;
    }

    public IComponent? QueryInterface(uint ent, int iid)
        => _byInterface.TryGetValue((ent, iid), out var c) ? c : null;

    public void PostMessage(uint ent, CMessage msg)
    {
        if (_localSubs.TryGetValue(msg.GetTypeId(), out var subs))
            foreach (var cid in subs)
                if (_byType.TryGetValue((ent, cid), out var comp) && comp is IMessageHandler h) h.HandleMessage(msg, false);
        SendGlobal(ent, msg);
    }
    public void BroadcastMessage(CMessage msg)
    {
        if (_localSubs.TryGetValue(msg.GetTypeId(), out var subs))
            foreach (var cid in subs)
                foreach (var kv in _byType) if (kv.Key.Item2 == cid && kv.Value is IMessageHandler h) h.HandleMessage(msg, false);
        SendGlobal(Entity.Invalid, msg);
    }
    private void SendGlobal(uint ent, CMessage msg)
    {
        if (!_globalSubs.TryGetValue(msg.GetTypeId(), out var subs)) return;
        foreach (var cid in subs)
            foreach (var kv in _byType) if (kv.Key.Item2 == cid && kv.Value is IMessageHandler h) h.HandleMessage(msg, true);
    }

    public void DestroyComponentsSoon(uint ent) => _destructionQueue.Add(ent);
    public void FlushDestroyedComponents()
    {
        foreach (var ent in _destructionQueue.ToArray())
        {
            PostMessage(ent, new CMessageDestroy(ent));
            var toRemove = new List<(uint,int)>();
            foreach (var kv in _byType) if (kv.Key.Item1 == ent) toRemove.Add(kv.Key);
            foreach (var k in toRemove) _byType.Remove(k);
            toRemove.Clear();
            foreach (var kv in _byInterface) if (kv.Key.Item1 == ent) toRemove.Add(kv.Key);
            foreach (var k in toRemove) _byInterface.Remove(k);
            _caches.Remove(ent);
        }
        _destructionQueue.Clear();
    }
    public void ResetState()
    {
        _byInterface.Clear(); _byType.Clear(); _caches.Clear(); _destructionQueue.Clear();
        _nextEntityId = Entity.SystemEntity + 1; _nextLocalId = Entity.FirstLocal;
        _systemHandle = default;
    }
}
