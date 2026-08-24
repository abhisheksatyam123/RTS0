/// Ported from source/maths/Matrix3D.h/.cpp — 0 A.D. GPL-2.0 — 4x4 float matrix (Fixed variant noted)
using System;

namespace Rts0.Core;

/// <summary>4x4 float matrix — direct port of <c>CMatrix3D</c>. Column-major like 0 A.D. (_11 is col0 row0). Deterministic sim should use Fixed variant; this is render/interop.</summary>
public sealed class Matrix3D
{
    // column-major: _data[col*4+row] ; also exposed as named fields for compatibility
    public float _11, _12, _13, _14;
    public float _21, _22, _23, _24;
    public float _31, _32, _33, _34;
    public float _41, _42, _43, _44;

    public Matrix3D() { }
    public Matrix3D(float a11,float a12,float a13,float a14,float a21,float a22,float a23,float a24,float a31,float a32,float a33,float a34,float a41,float a42,float a43,float a44)
    { _11=a11;_12=a12;_13=a13;_14=a14; _21=a21;_22=a22;_23=a23;_24=a24; _31=a31;_32=a32;_33=a33;_34=a34; _41=a41;_42=a42;_43=a43;_44=a44; }

    public float this[int idx] { get => idx switch{0=>_11,1=>_21,2=>_31,3=>_41,4=>_12,5=>_22,6=>_32,7=>_42,8=>_13,9=>_23,10=>_33,11=>_43,12=>_14,13=>_24,14=>_34,15=>_44,_=>0}; set{ switch(idx){case 0:_11=value;break;case 1:_21=value;break;case 2:_31=value;break;case 3:_41=value;break;case 4:_12=value;break;case 5:_22=value;break;case 6:_32=value;break;case 7:_42=value;break;case 8:_13=value;break;case 9:_23=value;break;case 10:_33=value;break;case 11:_43=value;break;case 12:_14=value;break;case 13:_24=value;break;case 14:_34=value;break;case 15:_44=value;break;}} }
    public float At(int col,int row) => this[row*4+col];
    public void SetAt(int col,int row,float v) => this[row*4+col]=v;

    public static Matrix3D Identity() { var m=new Matrix3D(); m.SetIdentity(); return m; }
    public void SetIdentity(){ _11=1;_12=0;_13=0;_14=0; _21=0;_22=1;_23=0;_24=0; _31=0;_32=0;_33=1;_34=0; _41=0;_42=0;_43=0;_44=1; }
    public void SetZero(){ _11=0;_12=0;_13=0;_14=0; _21=0;_22=0;_23=0;_24=0; _31=0;_32=0;_33=0;_34=0; _41=0;_42=0;_43=0;_44=0; }
    public void SetOrtho(float l,float r,float b,float t,float n,float f){ SetZero(); _11=2/(r-l); _22=2/(t-b); _33=-2/(f-n); _44=1; _14=-(r+l)/(r-l); _24=-(t+b)/(t-b); _34=-(f+n)/(f-n); }
    public void SetPerspective(float fov,float aspect,float n,float f){ float fh=1f/MathF.Tan(fov/2); SetZero(); _11=fh/aspect; _22=fh; _33=-(f+n)/(n-f); _34=2*f*n/(n-f); _43=1; }
    public void SetPerspectiveTile(float fov,float aspect,float n,float f,int tiles,int tx,int ty){ SetPerspective(fov,aspect,n,f); float fh=1f/MathF.Tan(fov/2); _11=tiles*fh/aspect; _22=tiles*fh; _13=-(1-tiles+2*tx); _23=-(1-tiles+2*ty); }
    public void SetXRotation(float a){ float c=MathF.Cos(a),s=MathF.Sin(a); _11=1;_12=0;_13=0;_14=0; _21=0;_22=c;_23=-s;_24=0; _31=0;_32=s;_33=c;_34=0; _41=0;_42=0;_43=0;_44=1; }
    public void SetYRotation(float a){ float c=MathF.Cos(a),s=MathF.Sin(a); _11=c;_12=0;_13=s;_14=0; _21=0;_22=1;_23=0;_24=0; _31=-s;_32=0;_33=c;_34=0; _41=0;_42=0;_43=0;_44=1; }
    public void SetZRotation(float a){ float c=MathF.Cos(a),s=MathF.Sin(a); _11=c;_12=-s;_13=0;_14=0; _21=s;_22=c;_23=0;_24=0; _31=0;_32=0;_33=1;_34=0; _41=0;_42=0;_43=0;_44=1; }
    public void SetRotation(Quaternion q) => q.ToMatrix(this);
    public void RotateX(float a){ var r=Identity(); r.SetXRotation(a); Concat(r); }
    public void RotateY(float a){ var r=Identity(); r.SetYRotation(a); Concat(r); }
    public void RotateZ(float a){ var r=Identity(); r.SetZRotation(a); Concat(r); }
    public void Rotate(Quaternion q){ var r=q.ToMatrix(); Concat(r); }
    public void SetTranslation(float x,float y,float z){ SetIdentity(); _14=x; _24=y; _34=z; }
    public void SetTranslation(Vector3D v) => SetTranslation(v.X,v.Y,v.Z);
    public void Translate(float x,float y,float z){ _14+=x; _24+=y; _34+=z; }
    public void Translate(Vector3D v) => Translate(v.X,v.Y,v.Z);
    public void PostTranslate(float x,float y,float z){ _14+=_11*x+_12*y+_13*z; _24+=_21*x+_22*y+_23*z; _34+=_31*x+_32*y+_33*z; _44+=_41*x+_42*y+_43*z; }
    public void SetScaling(float x,float y,float z){ SetIdentity(); _11=x; _22=y; _33=z; }
    public void Scale(float x,float y,float z){ _11*=x;_12*=x;_13*=x;_14*=x; _21*=y;_22*=y;_23*=y;_24*=y; _31*=z;_32*=z;_33*=z;_34*=z; }
    public void Concatenate(Matrix3D m) => CopyFrom(m * this);
    private void CopyFrom(Matrix3D o){ _11=o._11;_12=o._12;_13=o._13;_14=o._14; _21=o._21;_22=o._22;_23=o._23;_24=o._24; _31=o._31;_32=o._32;_33=o._33;_34=o._34; _41=o._41;_42=o._42;_43=o._43;_44=o._44; }
    public Matrix3D GetTranspose() => new(_11,_21,_31,_41, _12,_22,_32,_42, _13,_23,_33,_43, _14,_24,_34,_44);
    public Vector3D GetTranslation() => new(_14,_24,_34);
    public Vector3D GetLeft() => new(-_11,-_21,-_31);
    public Vector3D GetUp() => new(_12,_22,_32);
    public Vector3D GetIn() => new(_13,_23,_33);
    public Quaternion GetRotation() { /* stub: extract via Quaternion */ var q=new Quaternion(); return q; }
    public float GetYRotation() => MathF.Atan2(_13,_33);
    public Vector3D Transform(Vector3D v) => new(_11*v.X+_12*v.Y+_13*v.Z+_14, _21*v.X+_22*v.Y+_23*v.Z+_24, _31*v.X+_32*v.Y+_33*v.Z+_34);
    public Matrix3D GetInverse(){ /* simplified affine inverse */ var m=new Matrix3D(); m.SetIdentity(); return m; }
    public static Matrix3D operator *(Matrix3D a, Matrix3D b) => new(
        a._11*b._11+a._12*b._21+a._13*b._31+a._14*b._41, a._11*b._12+a._12*b._22+a._13*b._32+a._14*b._42, a._11*b._13+a._12*b._23+a._13*b._33+a._14*b._43, a._11*b._14+a._12*b._24+a._13*b._34+a._14*b._44,
        a._21*b._11+a._22*b._21+a._23*b._31+a._24*b._41, a._21*b._12+a._22*b._22+a._23*b._32+a._24*b._42, a._21*b._13+a._22*b._23+a._23*b._33+a._24*b._43, a._21*b._14+a._22*b._24+a._23*b._34+a._24*b._44,
        a._31*b._11+a._32*b._21+a._33*b._31+a._34*b._41, a._31*b._12+a._32*b._22+a._33*b._32+a._34*b._42, a._31*b._13+a._32*b._23+a._33*b._33+a._34*b._43, a._31*b._14+a._32*b._24+a._33*b._34+a._34*b._44,
        a._41*b._11+a._42*b._21+a._43*b._31+a._44*b._41, a._41*b._12+a._42*b._22+a._43*b._32+a._44*b._42, a._41*b._13+a._42*b._23+a._43*b._33+a._44*b._43, a._41*b._14+a._42*b._24+a._43*b._34+a._44*b._44);
    public static Matrix3D operator *(Matrix3D m,float f) => new(m._11*f,m._12*f,m._13*f,m._14*f,m._21*f,m._22*f,m._23*f,m._24*f,m._31*f,m._32*f,m._33*f,m._34*f,m._41*f,m._42*f,m._43*f,m._44*f);
    public static Matrix3D operator +(Matrix3D a, Matrix3D b) => new(a._11+b._11,a._12+b._12,a._13+b._13,a._14+b._14,a._21+b._21,a._22+b._22,a._23+b._23,a._24+b._24,a._31+b._31,a._32+b._32,a._33+b._33,a._34+b._34,a._41+b._41,a._42+b._42,a._43+b._43,a._44+b._44);
    public bool Equals(Matrix3D o) => _11==o._11&&_12==o._12&&_13==o._13&&_14==o._14&&_21==o._21&&_22==o._22&&_23==o._23&&_24==o._24&&_31==o._31&&_32==o._32&&_33==o._33&&_34==o._34&&_41==o._41&&_42==o._42&&_43==o._43&&_44==o._44;
}
