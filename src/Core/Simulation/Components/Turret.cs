/// Ported from /source/mods/public/simulation/components/TurretHolder.js + Turretable.js — 0 A.D. GPL-2.0
using System.Collections.Generic;
using System.Linq;
using Godot;

namespace Rts0.Core;

public sealed class TurretPoint
{
    public string Name="";
    public Vector3 Offset;
    public string AllowedClasses="";
    public float? Angle;
    public int? Entity;
    public string Template="";
    public bool Ejectable=true;
}

/// <summary>Holds turret points on a building/ship — mirrors TurretHolder.js.</summary>
public sealed class TurretHolderComponent
{
    public List<TurretPoint> Points=new();
    public float LoadingRange=2f;
    public bool Pickup;

    public TurretPoint? ByName(string n)=>Points.Find(p=>p.Name==n);
    public IReadOnlyList<int> GetEntities()=>Points.Where(p=>p.Entity!=null).Select(p=>p.Entity!.Value).ToList();
    public bool IsFull()=>Points.Any(p=>p.Entity==null);
    public (float min,float max) GetLoadingRange()=>(0,LoadingRange);

    public bool CanOccupy(int e, HashSet<string> classes)
        => Points.Any(p=> p.Entity==null && IsAllowed(e,classes,p));

    private bool IsAllowed(int e, HashSet<string> classes, TurretPoint p)
    {
        if(!string.IsNullOrEmpty(p.AllowedClasses))
            return p.AllowedClasses.Split(' ').Any(c=>classes.Contains(c));
        return true;
    }
    public bool Occupy(int e, string pointName="")
    {
        TurretPoint? pt = string.IsNullOrEmpty(pointName) ? Points.Find(p=>p.Entity==null) : ByName(pointName);
        if(pt==null || pt.Entity!=null) return false;
        pt.Entity=e; return true;
    }
    public bool Leave(int e, bool forced=false)
    {
        var pt=Points.Find(p=>p.Entity==e);
        if(pt==null || (!pt.Ejectable && !forced)) return false;
        pt.Entity=null; return true;
    }
}

/// <summary>Unit that can occupy a turret — mirrors Turretable.js.</summary>
public sealed class TurretableComponent
{
    public int Holder=-1;
    public bool Ejectable=true;
    public bool IsTurreted=>Holder!=-1;
    public (float min,float max) GetRange(TurretHolderComponent? holder)
        => holder!=null? holder.GetLoadingRange() : (0,1);
    public bool CanOccupy(int target, TurretHolderComponent holder)=> Holder==-1 && holder.CanOccupy(-1,new HashSet<string>());
    public bool OccupyTurret(int target, TurretHolderComponent holder, string point="", bool ejectable=true)
    {
        if(Holder!=-1) return false;
        if(!holder.Occupy(-1, point)) return false;
        Holder=target; Ejectable=ejectable; return true;
    }
    public bool LeaveTurret(TurretHolderComponent? holder, bool forced=false)
    {
        if(Holder==-1) return true;
        if(!Ejectable && !forced) return false;
        if(holder!=null && !holder.Leave(-1,forced)) return false;
        Holder=-1; return true;
    }
}
