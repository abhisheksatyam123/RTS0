/// Ported from source/simulation2/helpers/CinemaPath.h/.cpp — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Cinema path data — mirrors <c>CCinemaData</c>.</summary>
public class CinemaData
{
    public string Name = "", Orientation = "", Mode = "ease_inout", Style = "default";
    public bool LookAtTarget;
    public Fixed Timescale = Fixed.One;
    public float GrowthCount, Growth = 1f, Switch = 1f;
}

/// <summary>Spline node — position + duration + rotation.</summary>
public sealed class CinemaNode
{
    public Vector3D Position; public Fixed Distance; public Vector3D Rotation;
}

/// <summary>Cinema path — mirrors <c>CCinemaPath</c> + TNSpline (simplified linear spline).</summary>
public sealed class CinemaPath : CinemaData
{
    public List<CinemaNode> Nodes = new();
    public List<CinemaNode> TargetNodes = new();
    public float TimeElapsed, PreviousNodeTime;
    public int CurrentNode; public Vector3D PreviousRotation;
    public Fixed MaxDistance { get { Fixed s=Fixed.Zero; foreach(var n in Nodes) s+=n.Distance; return s; } }

    public CinemaPath() { }
    public CinemaPath(CinemaData d, IEnumerable<CinemaNode> spline, IEnumerable<CinemaNode> target)
    {
        Name=d.Name; Orientation=d.Orientation; Mode=d.Mode; Style=d.Style; LookAtTarget=d.Orientation=="target";
        Timescale=d.Timescale; Growth=d.Growth; Switch=d.Switch; GrowthCount=d.GrowthCount;
        if(spline!=null) Nodes.AddRange(spline);
        if(target!=null) TargetNodes.AddRange(target);
    }
    public Vector3D GetNodePosition(int i) => Nodes[i].Position;
    public Fixed GetNodeDuration(int i) => Nodes[i].Distance;
    public Fixed GetDuration() => MaxDistance;
    public float GetNodeFraction() => Nodes.Count==0?0:(TimeElapsed-PreviousNodeTime)/Math.Max(Nodes[CurrentNode].Distance.ToFloat(),1e-6f);
    public float GetElapsedTime() => TimeElapsed;
    public string GetName() => Name;
    public void SetTimescale(Fixed s) => Timescale=s;
    public Fixed GetTimescale() => Timescale;
    public bool Empty() => Nodes.Count==0;
    public void Reset(){ TimeElapsed=0; PreviousNodeTime=0; CurrentNode=0; }
    public Vector3D GetPosition(float t) // simplified lerp along nodes by distance
    {
        if(Nodes.Count==0) return Vector3D.Zero;
        if(Nodes.Count==1) return Nodes[0].Position;
        float total=GetDuration().ToFloat(); float d=t*total, acc=0;
        for(int i=0;i<Nodes.Count-1;i++){ float seg=Nodes[i].Distance.ToFloat(); if(d<=acc+seg){ float f=(d-acc)/Math.Max(seg,1e-6f); var a=Nodes[i].Position; var b=Nodes[i+1].Position; return a+(b-a)*f; } acc+=seg; }
        return Nodes[Nodes.Count-1].Position;
    }
    public void MoveToPointAt(float t,float nt,Vector3D startRot, Action<Vector3D,Vector3D> setCam)
    {
        t=DistMode(t);
        var pos=GetPosition(t);
        if(LookAtTarget){ Vector3D target=TargetNodes.Count>0? TargetNodes[TargetNodes.Count-1].Position: pos+new Vector3D(0,0,1); setCam(pos,target); }
        else{ float yaw=MathUtil.Lerp(startRot.Y, Nodes[Math.Min(CurrentNode+1,Nodes.Count-1)].Rotation.Y, nt); setCam(pos,new Vector3D(0,yaw,0)); }
    }
    // distortion modes
    public float EaseIn(float t)=> EaseStyle(t);
    public float EaseOut(float t)=> 1 - EaseStyle(1-t);
    public float EaseInOut(float t)=> t<Switch ? EaseStyle(t/Switch)*Switch : EaseOut((t-Switch)/Switch)*Switch+Switch;
    public float EaseOutIn(float t)=> t<Switch ? EaseOut(t/Switch)*Switch : EaseIn((t-Switch)/Switch)*Switch+Switch;
    public float EaseDefault(float t)=> t;
    public float EaseGrowth(float t)=> MathF.Pow(t,Growth);
    public float EaseExpo(float t)=> t==0?0:MathF.Pow(Growth,10*(t-1));
    public float EaseCircle(float t){ t=-(MathF.Sqrt(1-t*t)-1); if(GrowthCount>1){ GrowthCount--; return EaseStyle(t); } return t; }
    public float EaseSine(float t){ t=1-MathF.Cos(t*MathF.PI/2); if(GrowthCount>1){ GrowthCount--; return EaseStyle(t); } return t; }
    private float EaseStyle(float t)=> Style switch{ "growth"=>EaseGrowth(t), "expo"=>EaseExpo(t), "circle"=>EaseCircle(t), "sine"=>EaseSine(t), _=>EaseDefault(t) };
    private float DistMode(float t)=> Mode switch{ "ease_in"=>EaseIn(t), "ease_out"=>EaseOut(t), "ease_inout"=>EaseInOut(t), "ease_outin"=>EaseOutIn(t), _=>EaseInOut(t) };
    public bool Validate()
    {
        if(TimeElapsed> GetDuration().ToFloat() || TimeElapsed<0) return false;
        float prev=0, acc=0;
        for(int i=0;i<Nodes.Count-1;i++){ acc+=Nodes[i].Distance.ToFloat(); if(TimeElapsed<=acc){ PreviousNodeTime=prev; PreviousRotation=Nodes[i].Rotation; CurrentNode=i; return true; } prev+=Nodes[i].Distance.ToFloat(); }
        return Nodes.Count>0;
    }
    public bool Play(float dt, Action<Vector3D,Vector3D> setCam)
    {
        TimeElapsed+=Timescale.ToFloat()*dt;
        if(!Validate()) return false;
        MoveToPointAt(TimeElapsed/Math.Max(GetDuration().ToFloat(),1e-6f), GetNodeFraction(), PreviousRotation, setCam);
        return true;
    }
}
