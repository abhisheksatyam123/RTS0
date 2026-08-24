/// Ported from source/maths/Ease.h — 0 A.D. GPL-2.0 — Robert Penner easing
using System;

namespace Rts0.Core;

/// <summary>Penner easing — port of <c>Ease</c> (maths/Ease.h). Params: t in [0,d], b baseline, c delta, d duration.</summary>
public static class Ease
{
    public static float QuadIn(float t,float b,float c,float d){ t/=d; return c*t*t+b; }
    public static float QuadOut(float t,float b,float c,float d){ t/=d; return -c*t*(t-2)+b; }
    public static float QuadInOut(float t,float b,float c,float d){ t/=d/2; if(t<1) return c/2*t*t+b; t--; return -c/2*(t*(t-2)-1)+b; }
    public static float CubicIn(float t,float b,float c,float d){ t/=d; return c*t*t*t+b; }
    public static float CubicOut(float t,float b,float c,float d){ t=t/d-1; return c*(t*t*t+1)+b; }
    public static float CubicInOut(float t,float b,float c,float d){ t/=d/2; if(t<1) return c/2*t*t*t+b; t-=2; return c/2*(t*t*t+2)+b; }
    public static float QuartIn(float t,float b,float c,float d){ t/=d; return c*t*t*t*t+b; }
    public static float QuartOut(float t,float b,float c,float d){ t=t/d-1; return -c*(t*t*t*t-1)+b; }
    public static float QuartInOut(float t,float b,float c,float d){ t/=d/2; if(t<1) return c/2*t*t*t*t+b; t-=2; return -c/2*(t*t*t*t-2)+b; }
    public static float QuintIn(float t,float b,float c,float d){ t/=d; return c*t*t*t*t*t+b; }
    public static float QuintOut(float t,float b,float c,float d){ t=t/d-1; return c*(t*t*t*t*t+1)+b; }
    public static float QuintInOut(float t,float b,float c,float d){ t/=d/2; if(t<1) return c/2*t*t*t*t*t+b; t-=2; return c/2*(t*t*t*t*t+2)+b; }
    // helpers with normalized t in [0,1]
    public static float Lerp(float a,float b,float t) => a+(b-a)*t;
    public static float EaseIn(float t,string style,float growth=2f)
    {
        return style switch{ "growth"=>MathF.Pow(t,growth), "expo"=>t==0?0:MathF.Pow(growth,10*(t-1)), "circle"=>-(MathF.Sqrt(1-t*t)-1), "sine"=>1-MathF.Cos(t*MathF.PI/2), _=>t };
    }
}
