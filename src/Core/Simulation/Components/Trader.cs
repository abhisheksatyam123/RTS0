/// Ported from /source/mods/public/simulation/components/Trader.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class TraderComponent
{
    public float GainMultiplier=1f;
    public float? GarrisonGainMultiplier;
    public List<int> Markets=new();
    public int Index=-1;
    public string GoodsType="";
    public int GoodsAmount;

    public TraderComponent(float gain=1f,float? garrisonGain=null){ GainMultiplier=gain; GarrisonGainMultiplier=garrisonGain; }

    public int? GetFirstMarket()=>Markets.Count>0?Markets[0]:null;
    public int? GetSecondMarket()=>Markets.Count>1?Markets[1]:null;
    public bool HasBothMarkets()=>Markets.Count>=2;

    public bool CanTrade(int market, HashSet<string> myClasses, string marketType, System.Func<int,int,bool> isAlly, int myOwner, int marketOwner)
    {
        bool ok = (myClasses.Contains("Organic") && marketType=="land") || (myClasses.Contains("Ship") && marketType=="naval");
        if(!ok) return false;
        return isAlly(myOwner, marketOwner);
    }
    public bool SetTargetMarket(int target, int? source=null)
    {
        if(Markets.Count>=2){ Markets=new List<int>{target}; Index=0; GoodsAmount=0; return true; }
        if(Markets.Count==1){ if(target==Markets[0]) return false; Markets.Add(target); Index=0; return true; }
        Markets=new List<int>{target}; Index=0; return true;
    }
    public bool RemoveMarket(int m){ int i=Markets.IndexOf(m); if(i==-1) return false; Markets.RemoveAt(i); Index=-1; GoodsAmount=0; return true; }
    public void StopTrading(){ Markets.Clear(); Index=-1; GoodsAmount=0; }

    public int PerformTrade(int currentMarket, System.Func<int,int,float> distanceGain, System.Func<string> nextGoods, PlayerComponent player, int garrisonedTraders=0)
    {
        if(Markets.Count<2) return -1;
        int prev=Markets[Index];
        Index=(Index+1)%Markets.Count;
        int next=Markets[Index];
        // credit previous leg gain
        if(GoodsAmount>0) player.AddResource(GoodsType, GoodsAmount);
        GoodsType=nextGoods();
        float mult=1f + (GarrisonGainMultiplier??0)*garrisonedTraders;
        GoodsAmount=(int)(distanceGain(prev,next)*GainMultiplier*mult);
        return next;
    }
    public (float min,float max) GetRange(float obstructionSize=0)=>(0, 1+obstructionSize*1.5f);
}
