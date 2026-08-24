/// Ported from mods/public/simulation/components/RallyPoint.js + RallyPointRenderer analog — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;
using System.Numerics;

namespace Rts0.Core;

/// <summary>Mirrors RallyPoint.js — list of positions + command data.</summary>
public sealed class RallyPointComponent
{
    public sealed class RallyData
    {
        public string Command = "";
        public int? Target;
        public int? Source;
        public string? ResourceType;
    }

    private readonly List<Vector2> _pos = new();
    private readonly List<RallyData?> _data = new();
    // renderer state analog
    private readonly List<Vector2> _renderPos = new();

    public void AddPosition(float x,float z) { _pos.Add(new Vector2(x,z)); _renderPos.Add(new Vector2(x,z)); }
    public bool HasPositions => _pos.Count>0;
    public Vector2 GetFirstPosition() => _pos.Count>0 ? _pos[0] : new Vector2(-1,-1);
    public IReadOnlyList<Vector2> GetPositionsRaw() => _pos;

    public List<Vector2> GetPositions(System.Func<int,bool>? isAlive=null, System.Func<int,bool>? isVisible=null, System.Func<int,Vector2?> getPos=null)
    {
        var ret=new List<Vector2>(_pos);
        for(int i=0;i<_pos.Count;i++)
        {
            var d=_data.Count>i? _data[i]:null;
            if(d?.Target==null || (isAlive!=null && !isAlive(d.Target.Value))) continue;
            if(isVisible!=null && !isVisible(d.Target.Value)) continue;
            var p=getPos?.Invoke(d.Target.Value);
            if(p==null) continue;
            if(ret[i]==p.Value) continue;
            ret[i]=p.Value;
            if(i<_renderPos.Count) _renderPos[i]=p.Value;
        }
        return ret;
    }
    public void AddData(RallyData? d) => _data.Add(d);
    public IReadOnlyList<RallyData?> GetData() => _data;
    public void Unset() { _pos.Clear(); _data.Clear(); _renderPos.Clear(); }
    public void Reset() => Unset();

    public bool OrderToRallyPoint(int entity,int owner, System.Func<int,int,bool> sameOwner, System.Action<RallyData,Vector2> processCmd, string[]? ignore=null)
    {
        if(!sameOwner(entity,owner)) return false;
        if(_pos.Count==0) return false;
        // caller builds commands via GetRallyPointCommands; simplified: emit first
        var d=_data.Count>0? _data[0]:null;
        if(d!=null && ignore!=null && System.Array.IndexOf(ignore,d.Command)>=0) return false;
        if(d!=null) processCmd(d, _pos[0]);
        return true;
    }
    public void OnGlobalEntityRenamed(int oldEnt,int newEnt,int selfEnt, RallyPointComponent? newSelf)
    {
        foreach(var d in _data) if(d!=null){ if(d.Target==oldEnt) d.Target=newEnt; if(d.Source==oldEnt) d.Source=newEnt; }
        if(oldEnt!=selfEnt || newSelf==null) return;
        for(int i=0;i<_pos.Count;i++){ newSelf.AddPosition(_pos[i].X,_pos[i].Y); newSelf.AddData(_data[i]); }
    }
    public void OnOwnershipChanged(int from,int to) { if(from!=-1 && to!=-1) Reset(); }
}

/// <summary>Renderer analog — mirrors RallyPointRenderer.js visual marker positions.</summary>
public sealed class RallyPointRendererComponent
{
    private readonly List<Vector2> _markers = new();
    public IReadOnlyList<Vector2> Markers => _markers;
    public void UpdatePosition(int idx, Vector2 p) { if(idx>=0 && idx<_markers.Count) _markers[idx]=p; }
    public void Reset() => _markers.Clear();
    public void AddMarker(Vector2 p) => _markers.Add(p);
    public void SyncFrom(RallyPointComponent rp) { _markers.Clear(); _markers.AddRange(rp.GetPositionsRaw()); }
}
