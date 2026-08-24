/// Ported from /source/mods/public/simulation/components/Formation.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;
using Godot;

namespace Rts0.Core;

/// <summary>Formation offsets + member management. Mirrors Formation.js ComputeFormationOffsets.</summary>
public sealed class FormationComponent
{
    public int RequiredMemberCount = 2;
    public float SpeedMultiplier = 1f;
    public float MaxTurningAngle = MathF.PI/4;
    public bool ShiftRows;
    public float WidthDepthRatio = 1.5f;
    public float Sloppiness = 0.15f;
    public float SeparationWidth = 1f;
    public float SeparationDepth = 1f;
    public int MinColumns, MaxColumns, MaxRows;
    public float CenterGap;
    public string Shape = "square"; // square|triangle|special

    public List<int> Members = new();
    public Dictionary<int,Vector2> Offsets = new(); // entity -> local offset
    public float Width, Depth;
    public bool Columnar;

    private static readonly Random Rng = new(1337);

    public int MemberCount => Members.Count;
    public bool IsColumnar(float walkDistance) => walkDistance > 128f;

    public void SetMembers(IEnumerable<int> ents) { Members.Clear(); Members.AddRange(ents); ComputeOffsets(Vector2.Zero, 0); }
    public void AddMembers(IEnumerable<int> ents) { Members.AddRange(ents); ComputeOffsets(Vector2.Zero, 0); }
    public void RemoveMembers(IEnumerable<int> ents)
    {
        var s=new HashSet<int>(ents); Members.RemoveAll(e=>s.Contains(e));
        foreach(var e in s) Offsets.Remove(e);
        if(Members.Count>0) ComputeOffsets(Vector2.Zero, 0);
    }

    public Vector3 GetOffsetWorld(int entity, Vector2 center, float rot)
    {
        if(!Offsets.TryGetValue(entity, out var o)) return new Vector3(center.X,0,center.Y);
        float cos=MathF.Cos(rot), sin=MathF.Sin(rot);
        return new Vector3(center.X + o.Y*sin + o.X*cos, 0, center.Y + o.Y*cos - o.X*sin);
    }

    public void ComputeOffsets(Vector2 center, float rot, float walkDistance=0)
    {
        Columnar = IsColumnar(walkDistance);
        Offsets.Clear();
        int n = Members.Count; if (n==0) return;
        int cols = Columnar ? Math.Min(n,3) : ComputeCols(n);
        var positions = BuildGrid(n, cols, CenterGap, ShiftRows && !Columnar, Shape);
        // closest assignment: just order mapping (simplified vs 0 A.D. closest-search)
        for(int i=0;i<n && i<positions.Count;i++)
        {
            var p=positions[i];
            float rx = (float)(Rng.NextDouble()*2-1)*Sloppiness;
            float rz = (float)(Rng.NextDouble()*2-1)*Sloppiness;
            Offsets[Members[i]] = new Vector2(p.X+rx, p.Y+rz);
        }
        // compute width/depth
        float xMin=float.MaxValue,xMax=float.MinValue,yMin=float.MaxValue,yMax=float.MinValue;
        foreach(var kv in Offsets){ xMin=Math.Min(xMin,kv.Value.X); xMax=Math.Max(xMax,kv.Value.X); yMin=Math.Min(yMin,kv.Value.Y); yMax=Math.Max(yMax,kv.Value.Y);}
        Width=xMax-xMin; Depth=yMax-yMin;
    }

    private int ComputeCols(int n)
    {
        float depth=MathF.Sqrt(n/WidthDepthRatio);
        if(MaxRows>0 && depth>MaxRows) depth=MaxRows;
        int cols=(int)Math.Ceiling(n/Math.Ceiling(depth)+(ShiftRows?0.5:0));
        if(MinColumns>0 && cols<MinColumns) cols=Math.Min(n,MinColumns);
        if(MaxColumns>0 && cols>MaxColumns) cols=MaxColumns;
        return Math.Max(1,cols);
    }
    private List<Vector2> BuildGrid(int n,int cols,float gap,bool shift,string shape)
    {
        var pts=new List<Vector2>();
        int r=0,left=n;
        while(left>0)
        {
            float z=-r*SeparationDepth;
            int inRow = shape=="triangle" ? (shift? r+1 : r*2+1) : cols - (shift? r%2:0);
            inRow=Math.Min(inRow,left);
            for(int c=0;c<inRow;c++)
            {
                int side = (c%2==0)? 1:-1;
                float x;
                if(inRow%2==0) x=side*(c/2+0.5f)*SeparationWidth;
                else x=side*((c+1)/2)*SeparationWidth;
                if(gap>0 && x==0) continue;
                if(gap>0) x+=side*gap*0.5f;
                pts.Add(new Vector2(x,z));
            }
            left-=inRow; r++;
            if(MaxRows>0 && r>=MaxRows && left>0){ cols=Math.Max(cols,n/r); }
        }
        if(pts.Count==0) return pts;
        Vector2 avg=Vector2.Zero; foreach(var p in pts) avg+=p; avg/=pts.Count;
        for(int i=0;i<pts.Count;i++) pts[i]-=avg;
        return pts;
    }
}
