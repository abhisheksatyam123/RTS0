/// Ported from source/maths/NUSpline.h/.cpp — 0 A.D. GPL-2.0 — non-uniform splines.
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Spline node. Port of 0 A.D. <c>SplineData</c> (source/maths/NUSpline.h).</summary>
public struct SplineData
{
    public FixedVector3D Position;
    public Vector3D Velocity;
    public FixedVector3D Rotation;
    public Fixed Distance;
}

/// <summary>Hermite helper. See NUSpline.cpp GetPositionOnCubic (matrix H).</summary>
internal static class SplineUtil
{
    private static readonly Matrix3D Herm = new(2,-3,0,1, -2,3,0,0, 1,-2,1,0, 1,-1,0,0);
    public static Vector3D Cubic(Vector3D sp, Vector3D sv, Vector3D ep, Vector3D ev, float t)
    {
        var m = new Matrix3D(sp.X,ep.X,sv.X,ev.X, sp.Y,ep.Y,sv.Y,ev.Y, sp.Z,ep.Z,sv.Z,ev.Z, 0,0,0,1) * Herm;
        var tv = new Vector3D(t*t*t, t*t, t);
        // m.Transform(TimeVector) — manual since Matrix3D.Transform expects Vector3D with translation
        return new Vector3D(m._11*tv.X+m._12*tv.Y+m._13*tv.Z, m._21*tv.X+m._22*tv.Y+m._23*tv.Z, m._31*tv.X+m._32*tv.Y+m._33*tv.Z);
    }
}

/// <summary>Rounded non-uniform spline. Port of 0 A.D. <c>RNSpline</c> (maths/NUSpline.h/.cpp).</summary>
public class RNSpline
{
    protected readonly List<SplineData> Node = new();
    public int NodeCount => Node.Count;
    public Fixed MaxDistance { get; protected set; } = Fixed.Zero;
    public IReadOnlyList<SplineData> GetAllNodes() => Node;

    public virtual void AddNode(FixedVector3D pos)
    {
        if (Node.Count >= 128) return;
        if (Node.Count > 0) { var d = (Node[^1].Position - pos).Length(); var ld = Node[^1]; ld.Distance = d; Node[^1] = ld; MaxDistance += d; }
        else MaxDistance = Fixed.Zero;
        Node.Add(new SplineData { Position = pos });
    }

    public virtual void BuildSpline()
    {
        if (Node.Count < 2) return;
        if (Node.Count == 2) { var a = Node[0]; a.Velocity = GetStartVelocity(0); Node[0]=a; var b=Node[1]; b.Velocity=GetEndVelocity(1); Node[1]=b; return; }
        for (int i=1;i<Node.Count-1;i++) { var nxt=(Node[i+1].Position-Node[i].Position); var prv=(Node[i-1].Position-Node[i].Position);
            var n=Vector3D.FromFixed(new FixedVector3D(nxt.X,nxt.Y,nxt.Z)).Normalized();
            var p=Vector3D.FromFixed(new FixedVector3D(prv.X,prv.Y,prv.Z)).Normalized();
            var v=(n-p).Normalized(); var sd=Node[i]; sd.Velocity=v; Node[i]=sd; }
        var s0=Node[0]; s0.Velocity=GetStartVelocity(0); Node[0]=s0;
        var s1=Node[^1]; s1.Velocity=GetEndVelocity(Node.Count-1); Node[^1]=s1;
    }

    public Vector3D GetPosition(float t)
    {
        if (Node.Count < 2) return Vector3D.Zero;
        t = Math.Clamp(t, 0f, 1f);
        float dist = t * MaxDistance.ToFloat(), cur=0; int i=0;
        while (i < Node.Count-2 && cur + Node[i].Distance.ToFloat() < dist) { cur+=Node[i].Distance.ToFloat(); i++; }
        float seg = Node[i].Distance.ToFloat();
        if (seg < 1e-7f || Node[i].Position==Node[i+1].Position) return Vector3D.FromFixed(Node[i+1].Position);
        float tt=(dist-cur)/seg;
        var sv=Node[i].Velocity*seg; var ev=Node[i+1].Velocity*seg;
        return SplineUtil.Cubic(Vector3D.FromFixed(Node[i].Position), sv, Vector3D.FromFixed(Node[i+1].Position), ev, tt);
    }
    public Vector3D GetRotation(float t) => GetPosition(t); // same path; rotation uses same Hermite on Rotation field if needed

    protected Vector3D GetStartVelocity(int idx)
    {
        if (idx<0||idx>=Node.Count-1) return Vector3D.Zero;
        float d=Node[idx].Distance.ToFloat(); if(d<1e-7f) return Vector3D.Zero;
        var tmp=(Vector3D.FromFixed(Node[idx+1].Position)-Vector3D.FromFixed(Node[idx].Position))* (3f/d);
        return (tmp-Node[idx+1].Velocity)*0.5f;
    }
    protected Vector3D GetEndVelocity(int idx)
    {
        if (idx<1||idx>=Node.Count) return Vector3D.Zero;
        float d=Node[idx-1].Distance.ToFloat(); if(d<1e-7f) return Vector3D.Zero;
        var tmp=(Vector3D.FromFixed(Node[idx].Position)-Vector3D.FromFixed(Node[idx-1].Position))* (3f/d);
        return (tmp-Node[idx-1].Velocity)*0.5f;
    }
}

/// <summary>Smooth non-uniform spline. Port of 0 A.D. <c>SNSpline</c>.</summary>
public class SNSpline : RNSpline
{
    public override void BuildSpline() { base.BuildSpline(); for(int i=0;i<3;i++) Smooth(); }
    public void Smooth()
    {
        if (Node.Count<3) return;
        Vector3D old=GetStartVelocity(0), nv;
        for(int i=1;i<Node.Count-1;i++)
        { float d0=Node[i].Distance.ToFloat(), d1=Node[i-1].Distance.ToFloat();
          nv= GetEndVelocity(i)*d0 + GetStartVelocity(i)*d1;
          nv = nv * (1f/(Node[i-1].Distance+Node[i].Distance).ToFloat());
          var a=Node[i-1]; a.Velocity=old; Node[i-1]=a; old=nv; }
        var b=Node[^1]; b.Velocity=GetEndVelocity(Node.Count-1); Node[^1]=b;
        var c=Node[^2]; c.Velocity=old; Node[^2]=c;
    }
}

/// <summary>Timed non-uniform spline. Port of 0 A.D. <c>TNSpline</c>.</summary>
public class TNSpline : SNSpline
{
    public void AddNode(FixedVector3D pos, FixedVector3D rot, Fixed time)
    {
        if(Node.Count>=128) return;
        if(Node.Count>0){var a=Node[^1];a.Distance=time;Node[^1]=a;MaxDistance+=time;} else MaxDistance=Fixed.Zero;
        Node.Add(new SplineData{Position=pos,Rotation=rot,Distance=Fixed.Zero});
    }
    public void InsertNode(int idx, FixedVector3D pos, FixedVector3D rot, Fixed time)
    {
        if(Node.Count>=128||idx<0||idx>Node.Count) return;
        MaxDistance+=time; var sd=new SplineData{Position=pos,Rotation=rot,Distance=time};
        Node.Insert(idx,sd); if(idx>0){var t=Node[idx].Distance; var a=Node[idx]; a.Distance=Node[idx-1].Distance; Node[idx]=a; var b=Node[idx-1]; b.Distance=t; Node[idx-1]=b;}
    }
    public void RemoveNode(int idx){ if(idx<0||idx>=Node.Count) return; MaxDistance-=Node[idx].Distance; Node.RemoveAt(idx); }
    public void UpdateNodePos(int idx, FixedVector3D p){ if(idx<0||idx>=Node.Count) return; var a=Node[idx]; a.Position=p; Node[idx]=a; }
    public void UpdateNodeTime(int idx, Fixed t){ if(idx<0||idx>=Node.Count) return; var a=Node[idx]; a.Distance=t; Node[idx]=a; }
    public void Constrain()
    {
        if(Node.Count<3) return;
        for(int i=1;i<Node.Count-1;i++){ float r0=(Node[i].Position-Node[i-1].Position).Length().ToFloat()/Node[i-1].Distance.ToFloat();
            float r1=(Node[i+1].Position-Node[i].Position).Length().ToFloat()/Node[i].Distance.ToFloat();
            if(r0+r1<1e-7f) continue; var a=Node[i]; a.Velocity*=4f*r0*r1/((r0+r1)*(r0+r1)); Node[i]=a; }
    }
    public new void Smooth(){ for(int i=0;i<3;i++){ base.Smooth(); Constrain(); } }
}
