/// Ported from mods/public/simulation/components/Guard.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Guard.js — tracks guarded entities, range, ownership checks.</summary>
public sealed class GuardComponent
{
    private readonly List<int> _entities = new();
    public IReadOnlyList<int> Entities => _entities;

    public float GetRange(float footprintRadiusOrSize=0,bool isSquare=false,float w=0,float h=0)
    {
        float range=8f;
        if(isSquare) range+= MathF.Sqrt(w*w+h*h)*2f/3f;
        else if(footprintRadiusOrSize>0) range+= footprintRadiusOrSize*1.5f;
        return range;
    }
    public List<int> GetEntities() => new(_entities);
    public void SetEntities(IEnumerable<int> ents) { _entities.Clear(); _entities.AddRange(ents); }
    public void AddGuard(int ent) { if(!_entities.Contains(ent)) _entities.Add(ent); }
    public void RemoveGuard(int ent) => _entities.Remove(ent);
    public void RenameGuard(int oldEnt,int newEnt) { int i=_entities.IndexOf(oldEnt); if(i!=-1) _entities[i]=newEnt; }

    public void OnAttacked(object msg, System.Action<int,object> postGuarded)
    {
        foreach(var e in _entities) postGuarded(e, msg);
    }
    /// <summary>Mirrors Guard.js CheckGuards — removes guards no longer mutual allies.</summary>
    public void CheckGuards(System.Func<int,int,bool> isMutualAlly, System.Func<int,int?> guardOf, System.Action<int> removeGuardAI, bool force=false)
    {
        var copy=new List<int>(_entities);
        foreach(var ent in copy)
        {
            if(force || !isMutualAlly(-1,ent))
            {
                var g=guardOf(ent);
                if(g.HasValue && g.Value==-1) removeGuardAI(ent); // placeholder: entity id is not stored here; caller maps
                else RemoveGuard(ent);
            }
        }
    }
    public void OnOwnershipChanged(bool toInvalid) { if(_entities.Count>0) CheckGuards((a,b)=>!toInvalid, _=>null, _=>{} , toInvalid); }
    public void OnDiplomacyChanged(System.Func<int,int,bool> isMutualAlly, System.Func<int,int?> guardOf, System.Action<int> removeGuard)
        => CheckGuards(isMutualAlly, guardOf, removeGuard);
}
