/// Ported from /source/mods/public/simulation/components/Gate.js + WallPiece.js + WallSet.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Gate.js — auto open/close on ally proximity.</summary>
public sealed class GateComponent
{
    public float PassRange=20f;
    public bool Opened;
    public bool Locked;
    public List<int> Allies=new();
    public HashSet<int> Ignore=new();

    public float GetPassRange()=>PassRange;
    public bool ShouldOpen(){ foreach(var e in Allies) if(!Ignore.Contains(e)) return true; return false; }
    public void OnRangeAdded(IEnumerable<int> added, System.Func<int,bool> canMove)
    { foreach(var e in added){ Allies.Add(e); if(!canMove(e)) Ignore.Add(e); } Operate(); }
    public void OnRangeRemoved(IEnumerable<int> removed){ foreach(var e in removed){ Ignore.Remove(e); Allies.Remove(e);} Operate(); }
    public void OnAbleToMoveChanged(int e,bool able){ if(!Allies.Contains(e)) return; if(able) Ignore.Remove(e); else Ignore.Add(e); Operate(); }
    private void Operate()
    {
        if(Opened && (Locked || !ShouldOpen())) Close();
        else if(!Opened && !Locked && ShouldOpen()) Open();
    }
    public void Lock(){ Locked=true; if(!Opened) {/* block pathfinding */} else Operate(); }
    public void Unlock(){ Locked=false; Operate(); }
    public void Open(){ if(Locked) return; Opened=true; }
    public void Close(){ Opened=false; }
}

/// <summary>Mirrors WallPiece.js — geometry metadata.</summary>
public sealed class WallPieceComponent
{
    public float Length=4f;
    public float Orientation=1f; // * PI
    public float Indent;
    public float Bend;
}

/// <summary>Mirrors WallSet.js — template names for wall construction.</summary>
public sealed class WallSetComponent
{
    public string Tower="", Gate="", WallLong="", WallMedium="", WallShort="";
    public List<string> WallCurves=new();
    public string WallEnd="", Fort="";
    public float MinTowerOverlap, MaxTowerOverlap;
    public WallSetComponent(string tower="",string gate="",string wl="",string wm="",string ws="")
    { Tower=tower; Gate=gate; WallLong=wl; WallMedium=wm; WallShort=ws; }
}
