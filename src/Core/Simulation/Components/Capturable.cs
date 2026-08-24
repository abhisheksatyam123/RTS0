/// Ported from mods/public/simulation/components/Capturable.js + Capture attack — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

/// <summary>Mirrors Capturable.js — capture points per player, regen, territory decay.</summary>
public sealed class CapturableComponent
{
    public float MaxCapturePoints = 100;
    public float RegenRate = 1f;
    public float GarrisonRegenRate = 0.1f;
    public List<float> CapturePoints = new();
    public int? TimerId;

    public void Init(int numPlayers,float max,float regen,float garrisonRegen,int owner)
    {
        MaxCapturePoints=max; RegenRate=regen; GarrisonRegenRate=garrisonRegen;
        CapturePoints=new List<float>(new float[numPlayers]);
        if(owner>=0 && owner<numPlayers) CapturePoints[owner]=max;
    }
    public IReadOnlyList<float> GetCapturePoints()=>CapturePoints;
    public float GetMax()=>MaxCapturePoints;
    public void SetCapturePoints(List<float> v)=>CapturePoints=new List<float>(v);

    public bool CanCapture(int playerId, System.Func<int,bool> isEnemy)
    {
        float enemySum=0;
        for(int i=0;i<CapturePoints.Count;i++) if(CapturePoints[i]>0 && isEnemy(i)) enemySum+=CapturePoints[i];
        return enemySum>0;
    }
    public float Reduce(float amount,int playerId, System.Func<int,bool> isEnemy,int owner)
    {
        if(amount<=0 || owner==-1) return 0;
        int enemies=CapturePoints.Select((v,i)=> v>0 && isEnemy(i)?1:0).Sum();
        if(enemies==0) return 0;
        float distributed=amount/enemies; float removed=0;
        while(distributed>0.0001f)
        {
            int n=0;
            for(int i=0;i<CapturePoints.Count;i++)
            {
                if(CapturePoints[i]<=0 || !isEnemy(i)) continue;
                if(CapturePoints[i]>distributed){ removed+=distributed; CapturePoints[i]-=distributed; n++; }
                else { removed+=CapturePoints[i]; CapturePoints[i]=0; }
            }
            distributed = n>0 ? (amount-removed)/n : 0;
        }
        float taken=MaxCapturePoints-CapturePoints.Sum();
        CapturePoints[playerId]+=taken;
        return taken;
    }
    public float Capture(float amount,int captor,int captorOwner, System.Func<int,bool> isEnemy,int owner)
    {
        if(captorOwner==-1 || !CanCapture(captorOwner,isEnemy)) return 0;
        return Reduce(amount,captorOwner,isEnemy,owner);
    }
    public float GetRegenRate(int garrisonCount,float captureStrengthSum)
        => RegenRate + captureStrengthSum * GarrisonRegenRate;

    public bool CheckOwnershipChange(System.Func<int> getOwner, System.Action<int> setOwner, System.Action<int>? onLost=null, System.Action<int>? onCaptured=null)
    {
        int owner=getOwner();
        if(owner==-1 || CapturePoints[owner]>0) return false;
        onLost?.Invoke(owner);
        int best=0; for(int i=1;i<CapturePoints.Count;i++) if(CapturePoints[i]>CapturePoints[best]) best=i;
        setOwner(best);
        onCaptured?.Invoke(best);
        return true;
    }
    public void OnOwnershipChanged(int from,int to,int numPlayers)
    {
        if(CapturePoints.Count==0)
        {
            CapturePoints=new List<float>(new float[numPlayers]);
            if(to>=0 && to<numPlayers) CapturePoints[to]=MaxCapturePoints;
            return;
        }
        if(from>=0 && from<CapturePoints.Count && CapturePoints[from]>0 && to>=0)
        {
            CapturePoints[to]+=CapturePoints[from];
            CapturePoints[from]=0;
        }
    }
}

/// <summary>Mirage mirror.</summary>
public sealed class CapturableMirage
{
    public List<float> CapturePoints=new(); public float Max;
    public void Init(CapturableComponent c){ CapturePoints=new List<float>(c.CapturePoints); Max=c.MaxCapturePoints; }
}
