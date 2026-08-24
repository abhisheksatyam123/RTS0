/// Ported from mods/public/simulation/components/Pack.js — 0 A.D. GPL-2.0
using System;

namespace Rts0.Core;

/// <summary>Mirrors Pack.js — packed/unpacked + timed PackProgress.</summary>
public sealed class PackComponent
{
    public const int PackingIntervalMs = 250;
    public string EntityTemplate = ""; // Entity to transform into
    public int PackTimeMs = 2000;      // template Time
    public bool Packed;
    public bool Packing;
    public int ElapsedMs;
    public int? TimerId;

    public PackComponent(string template="",string state="packed",int timeMs=2000)
    {
        EntityTemplate=template;
        Packed = state=="packed";
        PackTimeMs=timeMs;
    }

    public bool IsPacked => Packed;
    public bool IsPacking => Packing;
    public bool CanPack() => !Packing && !Packed;
    public bool CanUnpack() => !Packing && Packed;

    public bool Pack(Func<int,int,int>? setTimer=null, Action<string,bool>? anim=null)
    {
        if(!CanPack()) return false;
        Packing=true;
        TimerId=setTimer?.Invoke(PackingIntervalMs, PackingIntervalMs);
        anim?.Invoke("packing",true);
        return true;
    }
    public bool Unpack(Func<int,int,int>? setTimer=null, Action<string,bool>? anim=null)
    {
        if(!CanUnpack()) return false;
        Packing=true;
        TimerId=setTimer?.Invoke(PackingIntervalMs, PackingIntervalMs);
        anim?.Invoke("unpacking",true);
        return true;
    }
    public void CancelPack(Action<int>? cancelTimer=null, Action<string,bool>? anim=null)
    {
        if(!IsPacking) return;
        if(TimerId.HasValue) cancelTimer?.Invoke(TimerId.Value);
        TimerId=null; Packing=false; SetElapsed(0);
        anim?.Invoke("idle",false);
    }
    public int GetPackTime() => PackTimeMs;
    public int GetElapsed() => ElapsedMs;
    public float GetProgress() => PackTimeMs==0?1:Math.Min((float)ElapsedMs/PackTimeMs,1f);
    public void SetElapsed(int t) => ElapsedMs=t;

    /// <summary>Timer callback — mirrors PackProgress. Returns new template if finished.</summary>
    public string? PackProgress(int lateness, Action<int>? cancelTimer=null)
    {
        if(ElapsedMs < GetPackTime())
        {
            SetElapsed(GetElapsed()+PackingIntervalMs+lateness);
            return null;
        }
        if(TimerId.HasValue) cancelTimer?.Invoke(TimerId.Value);
        TimerId=null;
        Packed=!Packed;
        Packing=false;
        return EntityTemplate;
    }
    public void CancelTimer(Action<int>? c) { if(TimerId.HasValue) c?.Invoke(TimerId.Value); TimerId=null; }
}
