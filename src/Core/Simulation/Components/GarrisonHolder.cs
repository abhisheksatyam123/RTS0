/// Ported from /source/mods/public/simulation/components/GarrisonHolder.js + Garrisonable.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>GarrisonHolder — capacity, class filtering, heal trickle, eject/kill logic.</summary>
public sealed class GarrisonHolderComponent
{
    public int Capacity = 10;
    public string AllowedClasses = ""; // tokens
    public string EjectClassesOnDestroy = "";
    public float BuffHeal = 0;
    public float LoadingRange = 2f;
    public float? EjectHealth; // fraction 0..1
    public bool Pickup;

    private readonly List<int> _entities = new();
    public IReadOnlyList<int> Entities => _entities;
    public int OccupiedSlots(Dictionary<int,int>? sizeByEnt=null)
    {
        if(sizeByEnt==null) return _entities.Count;
        int s=0; foreach(var e in _entities) s+= sizeByEnt.GetValueOrDefault(e,1); return s;
    }
    public bool IsFull(Dictionary<int,int>? sizes=null) => OccupiedSlots(sizes) >= Capacity;
    public bool IsInside(int e)=>_entities.Contains(e);
    public bool IsAllowed(int e, HashSet<string> classes)
    {
        if(string.IsNullOrWhiteSpace(AllowedClasses)) return true;
        foreach(var c in AllowedClasses.Split(' ')) if(classes.Contains(c)) return true;
        return false;
    }
    public bool CanGarrison(int e, HashSet<string> classes, Dictionary<int,int>? sizes=null, int size=1)
    {
        if(!IsAllowed(e,classes)) return false;
        return OccupiedSlots(sizes)+size <= Capacity;
    }
    public bool Garrison(int e, HashSet<string> classes, Dictionary<int,int>? sizes=null, int size=1)
    {
        if(!CanGarrison(e,classes,sizes,size)) return false;
        _entities.Add(e); return true;
    }
    public bool Eject(int e){ return _entities.Remove(e); }
    public List<int> EjectAll(){ var copy=new List<int>(_entities); _entities.Clear(); return copy; }
    public bool HasEnoughHealth(float hpFrac) => EjectHealth==null || hpFrac > EjectHealth.Value;
    public void HealTick(Dictionary<int,HealthComponent> hpByEnt)
    {
        if(BuffHeal<=0) return;
        foreach(var e in _entities) if(hpByEnt.TryGetValue(e,out var h)) h.Heal((int)BuffHeal);
    }
}

/// <summary>Garrisonable — size and holder reference. Mirrors Garrisonable.js.</summary>
public sealed class GarrisonableComponent
{
    public int Size = 1;
    public int Holder = -1; // INVALID_ENTITY
    public bool IsGarrisoned => Holder!=-1;
    public int TotalSize(Dictionary<int,GarrisonHolderComponent>? holders=null)
    {
        int s=Size;
        if(holders!=null && holders.TryGetValue(Holder,out var h)) s+=h.Entities.Count;
        return s;
    }
    public bool CanGarrison(int target, GarrisonHolderComponent holder, HashSet<string> myClasses, Dictionary<int,int>? sizes=null)
        => !IsGarrisoned && holder.CanGarrison(-1, myClasses, sizes, Size);
    public bool Garrison(int target, GarrisonHolderComponent holder, HashSet<string> myClasses)
    {
        if(!holder.Garrison(-1, myClasses)) return false;
        Holder=target; return true;
    }
    public bool UnGarrison(GarrisonHolderComponent? holder)
    {
        if(!IsGarrisoned) return true;
        holder?.Eject(-1);
        Holder=-1; return true;
    }
}

