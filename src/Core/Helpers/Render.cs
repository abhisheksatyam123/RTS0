/// Ported from source/simulation2/helpers/Render.h/.cpp — 0 A.D. GPL-2.0 — render overlay helpers (stub).
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Dashed line. Port of 0 A.D. <c>SDashedLine</c> (simulation2/helpers/Render.h).</summary>
public sealed class DashedLine
{
    public List<Vector2D> Points = new();
    public List<int> StartIndices = new();
    public int GetEndIndex(int i) => i < StartIndices.Count - 1 ? StartIndices[i + 1] : Points.Count;
}

/// <summary>Overlay line stub. Mirrors <c>SOverlayLine</c> (graphics/Overlay.h).</summary>
public sealed class OverlayLine
{
    public List<float> Coords = new();
    public Vector3D Color = new(1,1,1);
    public float Thickness = 1f;
    public void Clear() => Coords.Clear();
    public void PushCoords(float x,float y,float z){ Coords.Add(x); Coords.Add(y); Coords.Add(z); }
    public void PushCoords(Vector3D v) => PushCoords(v.X,v.Y,v.Z);
}

/// <summary>Textured line stub. Mirrors <c>SOverlayTexturedLine</c>.</summary>
public sealed class OverlayTexturedLine
{
    public List<Vector2D> Coords = new();
    public float Thickness = 1f;
    public bool Closed = true;
    public void PushCoords(float x,float z) => Coords.Add(new Vector2D(x,z));
    public void PushCoords(IEnumerable<Vector2D> pts) { foreach(var p in pts) Coords.Add(p); }
}

/// <summary>Render helpers. Port of 0 A.D. <c>SimRender::</c> (simulation2/helpers/Render.h/.cpp). Stub: terrain sampling replaced by y=0.</summary>
public static class SimRender
{
    public static void ConstructLineOnGround(IEnumerable<float> xz, OverlayLine o, bool floating=false, float ho=0.25f)
    {
        o.Clear(); var e=xz.GetEnumerator(); // xz = x0,z0,x1,z1...
        // fallback: iterate via array if possible
        if (xz is IList<float> list) { for(int i=0;i+1<list.Count;i+=2) o.PushCoords(list[i],ho,list[i+1]); return; }
        // enumerator path
        float? px=null; foreach(var v in xz){ if(px==null) px=v; else { o.PushCoords(px.Value,ho,v); px=null; } }
    }
    public static void ConstructCircleOnGround(float x,float z,float r, OverlayLine o, bool floating=false, float ho=0.25f)
        => ConstructCircleOrArc(x,z,r,true,0,2*MathF.PI,o,ho);
    public static void ConstructClosedArcOnGround(float x,float z,float r,float s,float e, OverlayLine o, bool floating=false, float ho=0.25f)
        => ConstructCircleOrArc(x,z,r,false,s,e,o,ho);
    private static void ConstructCircleOrArc(float x,float z,float r,bool circle,float s,float e, OverlayLine o,float ho)
    {
        o.Clear(); int n=Math.Clamp((int)(r*Math.Abs(e-s)),12,48);
        if(!circle) o.PushCoords(x,ho,z);
        for(int i=0;i<=n;i++){ float a=s+i*(e-s)/n; o.PushCoords(x+r*MathF.Cos(a),ho,z+r*MathF.Sin(a)); }
        if(!circle) o.PushCoords(x,ho,z);
    }
    public static void ConstructSquareOnGround(float x,float z,float w,float h,float a, OverlayLine o, bool floating=false, float ho=0.25f)
    {
        o.Clear(); float c=MathF.Cos(a), s=MathF.Sin(a);
        (float,float)[] pts={ (x-w/2*c+h/2*s, z+w/2*s+h/2*c),(x-w/2*c-h/2*s, z+w/2*s-h/2*c),(x+w/2*c-h/2*s, z-w/2*s-h/2*c),(x+w/2*c+h/2*s, z-w/2*s+h/2*c)};
        for(int i=0;i<4;i++){ o.PushCoords(pts[i].Item1,ho,pts[i].Item2); o.PushCoords(pts[(i+1)%4].Item1,ho,pts[(i+1)%4].Item2); }
    }
    public static void ConstructBoxOutline(BoundingBoxAligned b, OverlayLine o)
    {
        o.Clear(); if(b.Min==Vector3D.MaxValue) return;
        var mn=b.Min; var mx=b.Max;
        void P(float x,float y,float z)=>o.PushCoords(x,y,z);
        P(mn.X,mn.Y,mn.Z);P(mx.X,mn.Y,mn.Z);P(mx.X,mn.Y,mx.Z);P(mn.X,mn.Y,mx.Z);P(mn.X,mn.Y,mn.Z);
        P(mn.X,mx.Y,mn.Z);P(mx.X,mx.Y,mn.Z);P(mx.X,mx.Y,mx.Z);P(mn.X,mx.Y,mx.Z);P(mn.X,mx.Y,mn.Z);
    }
    public static void ConstructBoxOutline(BoundingBoxOriented b, OverlayLine o)
    {
        o.Clear(); if(b.IsEmpty()) return;
        Vector3D[] c=new Vector3D[8];
        for(int i=0;i<8;i++){ int ux=i&1, uy=(i>>1)&1, uz=(i>>2)&1; // map 0/1 to -1/1
            b.GetCorner(ux==0?-1:1, uy==0?-1:1, uz==0?-1:1, out c[i]); }
        for(int i=0;i<4;i++) o.PushCoords(c[i]); o.PushCoords(c[0]);
        for(int i=4;i<8;i++) o.PushCoords(c[i]); o.PushCoords(c[4]);
    }
    public static void ConstructGimbal(Vector3D center,float radius,OverlayLine o,int steps=16)
    {
        o.Clear(); float inc=2*MathF.PI/steps;
        var rot=new Vector3D(0,0,radius);
        var yAxis=new Vector3D(0,1,0); var zAxis=new Vector3D(0,0,1); var xAxis=new Vector3D(1,0,0);
        // simplified: three circles sequentially (not continuous like 0ad but visually similar)
        for(int i=0;i<steps;i++){ o.PushCoords(center+rot); rot=RotateY(rot,inc); }
        rot=new Vector3D(0,0,radius);
        for(int i=0;i<steps;i++){ o.PushCoords(center+rot); rot=RotateZ(rot,inc); }
        rot=new Vector3D(0,0,radius);
        for(int i=0;i<=steps;i++){ o.PushCoords(center+rot); rot=RotateX(rot,inc); }
    }
    private static Vector3D RotateY(Vector3D v,float a){ float c=MathF.Cos(a),s=MathF.Sin(a); return new Vector3D(v.X*c+v.Z*s, v.Y, -v.X*s+v.Z*c); }
    private static Vector3D RotateZ(Vector3D v,float a){ float c=MathF.Cos(a),s=MathF.Sin(a); return new Vector3D(v.X*c-v.Y*s, v.X*s+v.Y*c, v.Z); }
    private static Vector3D RotateX(Vector3D v,float a){ float c=MathF.Cos(a),s=MathF.Sin(a); return new Vector3D(v.X, v.Y*c-v.Z*s, v.Y*s+v.Z*c); }

    public static void ConstructAxesMarker(Matrix3D m, OverlayLine ox, OverlayLine oy, OverlayLine oz)
    {
        ox.Clear(); oy.Clear(); oz.Clear();
        var o=m.GetTranslation();
        ox.PushCoords(o); ox.PushCoords(o+new Vector3D(m._11,m._21,m._31));
        oy.PushCoords(o); oy.PushCoords(o+new Vector3D(m._12,m._22,m._32));
        oz.PushCoords(o); oz.PushCoords(o+new Vector3D(m._13,m._23,m._33));
    }
    public static void SmoothPointsAverage(List<Vector2D> pts,bool closed)
    {
        if(pts.Count<2) return; var n=pts.Count; var np=new Vector2D[n];
        if(closed){ np[0]=(pts[n-1]+pts[0]+pts[1])/3f; np[n-1]=(pts[n-2]+pts[n-1]+pts[0])/3f; } else { np[0]=pts[0]; np[n-1]=pts[n-1]; }
        for(int i=1;i<n-1;i++) np[i]=(pts[i-1]+pts[i]+pts[i+1])/3f;
        for(int i=0;i<n;i++) pts[i]=np[i];
    }
    public static void InterpolatePointsRNS(List<Vector2D> pts,bool closed,float offset,int seg=4)
    {
        if(closed? pts.Count<1: pts.Count<2) return;
        int n=pts.Count, imax=closed?n:n-1; var np=new List<Vector2D>(imax*seg);
        Vector2D a0=default,a1=default,a2=default,a3=default;
        for(int i=0;i<imax;i++)
        {
            Vector2D p0=(!closed&&i==0)? pts[0]+(pts[0]-pts[1]): pts[(i-1+n)%n];
            Vector2D p1=pts[i], p2=pts[(i+1)%n];
            Vector2D p3=(!closed&&i==n-2)? pts[n-1]+(pts[n-1]-pts[n-2]): pts[(i+2)%n];
            float l1=(p2-p1).Length();
            Vector2D s0=(p1-p0).Normalized(), s1=(p2-p1).Normalized(), s2=(p3-p2).Normalized();
            Vector2D v1=(s0+s1).Normalized()*l1, v2=(s1+s2).Normalized()*l1;
            a0=p1*2+p2*-2+v1+v2; a1=p1*-3+p2*3+v1*-2+v2*-1; a2=v1; a3=p1;
            for(int s=0;s<seg;s++) np.Add(EvalSpline(s/(float)seg,a0,a1,a2,a3,offset));
        }
        if(!closed) np.Add(EvalSpline(1f,a0,a1,a2,a3,offset));
        pts.Clear(); pts.AddRange(np);
    }
    private static Vector2D EvalSpline(float t,Vector2D a0,Vector2D a1,Vector2D a2,Vector2D a3,float off)
    {
        var p=a0*(t*t*t)+a1*(t*t)+a2*t+a3;
        var dp=(a0*(3*t*t)+a1*(2*t)+a2).Normalized();
        return p+new Vector2D(dp.Y*-off, dp.X*off);
    }
    public static void ConstructDashedLine(IReadOnlyList<Vector2D> pts, DashedLine o,float dl,float bl)
    {
        if(dl<=0||bl<=0||pts.Count<2) return;
        o.Points.Clear(); o.StartIndices.Clear();
        bool dash=true; float cur=0; var last=pts[0]; o.Points.Add(last); o.StartIndices.Add(0);
        int i=0;
        while(i<pts.Count-1)
        {
            var seg=pts[i+1]-last; float len=seg.Length(), target=dash?dl:bl;
            if(cur+len>target){ float cut=target-cur; var cp=last+seg.Normalized()*cut; cur=0; dash=!dash; last=cp; o.Points.Add(cp); if(dash) o.StartIndices.Add(o.Points.Count-1); }
            else { if(dash) o.Points.Add(pts[i+1]); cur+=len; last=pts[i+1]; i++; }
        }
    }
    public static void SubdividePoints(List<Vector2D> pts,float maxLen,bool closed)
    {
        if(pts.Count<2||maxLen<=0) return; int end=closed? pts.Count: pts.Count-1; if(!closed&&pts.Count<=2) end=pts.Count-1;
        var np=new List<Vector2D>();
        for(int i=0;i<end;i++){ var cur=pts[i]; var nxt=pts[(i+1)%pts.Count]; var dir=(nxt-cur).Normalized();
            np.Add(cur); float len=(nxt-cur).Length(); int segs=(int)MathF.Ceiling(len/maxLen); float sl=len/segs;
            for(int s=1;s<segs;s++) np.Add(cur+dir*(s*sl)); }
        pts.Clear(); pts.AddRange(np);
    }
    public static void ConstructTexturedLineBox(OverlayTexturedLine o, Vector2D origin, FixedVector3D rot,float sx,float sz)
    {
        float s=MathF.Sin(-rot.Y.ToFloat()), c=MathF.Cos(-rot.Y.ToFloat());
        var ux=new Vector2D(c,s); var uz=new Vector2D(-s,c);
        float hx=sx/2+o.Thickness/2, hz=sz/2+o.Thickness/2;
        var pts=new List<Vector2D>{ origin+ux*hx+uz*-hz, origin+ux*-hx+uz*-hz, origin+ux*-hx+uz*hz, origin+ux*hx+uz*hz };
        SubdividePoints(pts, 4f, o.Closed); o.Coords.Clear(); o.Coords.AddRange(pts);
    }
    public static void ConstructTexturedLineCircle(OverlayTexturedLine o, Vector2D origin,float r)
    {
        float rad=r+o.Thickness/3f; int n=(int)MathF.Ceiling(2*MathF.PI*rad/4f);
        o.Coords.Clear(); for(int i=0;i<n;i++){ float a=i*2*MathF.PI/n; o.PushCoords(origin.X+rad*MathF.Sin(a), origin.Y+rad*MathF.Cos(a)); }
    }
}
