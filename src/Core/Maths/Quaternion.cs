/// Ported from source/maths/Quaternion.h/.cpp — 0 A.D. GPL-2.0
using System;

namespace Rts0.Core;

/// <summary>Quaternion — port of <c>CQuaternion</c>. Uses float; normalize before rotate.</summary>
public sealed class Quaternion
{
    public Vector3D V; public float W;
    public Quaternion(){ V=Vector3D.Zero; W=1; }
    public Quaternion(float x,float y,float z,float w){ V=new Vector3D(x,y,z); W=w; }
    public static Quaternion operator +(Quaternion a, Quaternion b) => new(a.V.X+b.V.X,a.V.Y+b.V.Y,a.V.Z+b.V.Z,a.W+b.W);
    public static Quaternion operator -(Quaternion a, Quaternion b) => new(a.V.X-b.V.X,a.V.Y-b.V.Y,a.V.Z-b.V.Z,a.W-b.W);
    public static Quaternion operator *(Quaternion a, Quaternion b)
    {
        float w=a.W*b.W - a.V.Dot(b.V);
        var v=b.V*a.W + a.V*b.W + a.V.Cross(b.V);
        return new(v.X,v.Y,v.Z,w);
    }
    public static Quaternion operator *(Quaternion q,float f) => new(q.V.X*f,q.V.Y*f,q.V.Z*f,q.W*f);
    public float Dot(Quaternion o) => V.X*o.V.X+V.Y*o.V.Y+V.Z*o.V.Z+W*o.W;
    public void FromEulerAngles(float x,float y,float z)
    {
        float cr=MathF.Cos(x*0.5f), cp=MathF.Cos(y*0.5f), cy=MathF.Cos(z*0.5f);
        float sr=MathF.Sin(x*0.5f), sp=MathF.Sin(y*0.5f), sy=MathF.Sin(z*0.5f);
        var roll=new Quaternion(sr,0,0,cr); var pitch=new Quaternion(0,sp,0,cp); var yaw=new Quaternion(0,0,sy,cy);
        var q=yaw*pitch*roll; V=q.V; W=q.W;
    }
    public Vector3D ToEulerAngles()
    {
        const float eps=1e-4f; float sqw=W*W, sqx=V.X*V.X, sqy=V.Y*V.Y, sqz=V.Z*V.Z, unit=sqx+sqy+sqz+sqw, test=V.X*V.Y+V.Z*W;
        float heading, attitude, bank;
        if(test>(.5f-eps)*unit){ heading=2*MathF.Atan2(V.X,W); attitude=MathF.PI/2; bank=0; }
        else if(test<(-.5f+eps)*unit){ heading=-2*MathF.Atan2(V.X,W); attitude=-MathF.PI/2; bank=0; }
        else{ heading=MathF.Atan2(2*(V.X*V.Y+V.Z*W),(sqx-sqy-sqz+sqw)); bank=MathF.Atan2(2*(V.Y*V.Z+V.X*W),(-sqx-sqy+sqz+sqw)); attitude=MathF.Asin(-2*(V.X*V.Z-V.Y*W)); }
        return new Vector3D(bank,attitude,heading);
    }
    public Matrix3D ToMatrix(){ var m=new Matrix3D(); ToMatrix(m); return m; }
    public void ToMatrix(Matrix3D r)
    {
        float xx=V.X*V.X*2, xy=V.X*V.Y*2, xz=V.X*V.Z*2, yy=V.Y*V.Y*2, yz=V.Y*V.Z*2, zz=V.Z*V.Z*2, wx=W*V.X*2, wy=W*V.Y*2, wz=W*V.Z*2;
        r._11=1-(yy+zz); r._12=xy-wz; r._13=xz+wy; r._14=0;
        r._21=xy+wz; r._22=1-(xx+zz); r._23=yz-wx; r._24=0;
        r._31=xz-wy; r._32=yz+wx; r._33=1-(xx+yy); r._34=0;
        r._41=0; r._42=0; r._43=0; r._44=1;
    }
    public void Slerp(Quaternion from, Quaternion to, float t)
    {
        float cosom=from.Dot(to); float[] t1=new float[4]{to.V.X,to.V.Y,to.V.Z,to.W};
        if(cosom<0){ cosom=-cosom; t1[0]=-t1[0]; t1[1]=-t1[1]; t1[2]=-t1[2]; t1[3]=-t1[3]; }
        float s0,s1; if(1-cosom>1e-4f){ float om=MathF.Acos(cosom), sinom=MathF.Sin(om); s0=MathF.Sin((1-t)*om)/sinom; s1=MathF.Sin(t*om)/sinom; } else { s0=1-t; s1=t; }
        V=new Vector3D(s0*from.V.X+s1*t1[0], s0*from.V.Y+s1*t1[1], s0*from.V.Z+s1*t1[2]); W=s0*from.W+s1*t1[3];
    }
    public void Nlerp(Quaternion from, Quaternion to, float t)
    {
        float c=from.Dot(to);
        Quaternion q=c<0? new Quaternion(from.V.X-(to.V.X+from.V.X)*t, from.V.Y-(to.V.Y+from.V.Y)*t, from.V.Z-(to.V.Z+from.V.Z)*t, from.W-(to.W+from.W)*t)
                        : new Quaternion(from.V.X+(to.V.X-from.V.X)*t, from.V.Y+(to.V.Y-from.V.Y)*t, from.V.Z+(to.V.Z-from.V.Z)*t, from.W+(to.W-from.W)*t);
        V=q.V; W=q.W; Normalize();
    }
    public void FromAxisAngle(Vector3D axis,float angle){ float s=MathF.Sin(angle/2),c=MathF.Cos(angle/2); V=new Vector3D(axis.X*s,axis.Y*s,axis.Z*s); W=c; }
    public void ToAxisAngle(out Vector3D axis,out float angle){ Normalize(); angle=MathF.Acos(W)*2; float s=MathF.Sqrt(1-W*W); if(MathF.Abs(s)<0.0005f) s=1; axis=new Vector3D(V.X/s,V.Y/s,V.Z/s); }
    public void Normalize(){ float l=V.X*V.X+V.Y*V.Y+V.Z*V.Z+W*W; if(l>0){ float inv=1/MathF.Sqrt(l); V=new Vector3D(V.X*inv,V.Y*inv,V.Z*inv); W*=inv; } }
    public Vector3D Rotate(Vector3D v){ var q=new Quaternion(V.X,V.Y,V.Z,W); var qi=q.GetInverse(); var p=new Quaternion(v.X,v.Y,v.Z,0); var r=q*p*qi; return new Vector3D(r.V.X,r.V.Y,r.V.Z); }
    public Quaternion GetInverse() => new(-V.X,-V.Y,-V.Z,W);
}
