/// Ported from /source/mods/public/simulation/components/Promotion.js — 0 A.D. GPL-2.0
using System;

namespace Rts0.Core;

/// <summary>Mirrors Promotion.js — XP accumulation and template promotion.</summary>
public sealed class PromotionComponent
{
    public string PromotedTemplate="";
    public int RequiredXp=100;
    public float TrickleRate;
    public int CurrentXp;
    private float _trickleAccum;
    public int? PromotedEntity; // set after promotion pending destroy

    public PromotionComponent(string promoted="",int requiredXp=100,float trickle=0)
    {
        PromotedTemplate=promoted; RequiredXp=requiredXp; TrickleRate=trickle;
    }
    public int GetRequiredXp()=>RequiredXp;
    public int GetCurrentXp()=>CurrentXp;
    public string GetPromotedTemplateName()=>PromotedTemplate;

    /// <returns>promoted template name when threshold reached, else null</returns>
    public string? IncreaseXp(int amount, Func<string,string?>? nextTemplate=null)
    {
        if(PromotedEntity!=null) return null;
        CurrentXp+=amount;
        if(CurrentXp < RequiredXp) return null;
        CurrentXp-=RequiredXp;
        string promoted=PromotedTemplate;
        // chain promotions if enough xp (simplified)
        while(nextTemplate!=null)
        {
            var nxt=nextTemplate(promoted);
            if(nxt==null) break;
            // caller supplies RequiredXp for nxt; we reuse same threshold for brevity
            if(CurrentXp < RequiredXp) break;
            CurrentXp-=RequiredXp; promoted=nxt;
        }
        return promoted;
    }
    public bool Tick(float dt, out string? promoted)
    {
        promoted=null;
        if(TrickleRate<=0) return false;
        _trickleAccum+=dt;
        if(_trickleAccum>=1f){ _trickleAccum-=1f; promoted=IncreaseXp((int)TrickleRate); return promoted!=null; }
        return false;
    }
}
