/// Ported from mods/public/simulation/components/Market.js + Barter.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Market.js — traders, trade type, gain calc.</summary>
public sealed class MarketComponent
{
    public HashSet<string> TradeType = new();
    public float InternationalBonus = 0.1f;
    private readonly HashSet<int> _traders = new();
    public IReadOnlyCollection<int> Traders => _traders;

    public MarketComponent(string tradeType="land",float bonus=0.1f)
    { foreach(var t in tradeType.Split(' ',System.StringSplitOptions.RemoveEmptyEntries)) TradeType.Add(t); InternationalBonus=bonus; }

    public void AddTrader(int e)=>_traders.Add(e);
    public void RemoveTrader(int e)=>_traders.Remove(e);
    public bool HasType(string t)=>TradeType.Contains(t);
    public IReadOnlyCollection<string> GetTradeType()=>TradeType;
    public float GetInternationalBonus()=>InternationalBonus;

    public void UpdateTraders(bool onDestruction, System.Func<int,bool> canTrade, System.Func<int,bool> hasMarket, System.Action<int> removeMarket)
    {
        var copy=new List<int>(_traders);
        foreach(var tr in copy)
        {
            if(!hasMarket(tr)){ _traders.Remove(tr); continue; }
            if(!onDestruction && canTrade(tr)) continue;
            if(!canTrade(tr)){ _traders.Remove(tr); removeMarket(tr); }
        }
    }
    public float CalculateTraderGain(float dist, float mapSize, float traderMultiplier=1f)
    {
        // mirrors TradeGain = square distance normalized
        float gain = traderMultiplier * dist * dist / 10000f; // simplified
        return MathF.Round(gain);
    }
}

/// <summary>Mirrors MarketMirage helper.</summary>
public sealed class MarketMirage
{
    public HashSet<int> Traders=new(); public HashSet<string> MarketType=new(); public float InternationalBonus;
    public bool HasType(string t)=>MarketType.Contains(t);
    public void AddTrader(int e)=>Traders.Add(e);
    public void RemoveTrader(int e)=>Traders.Remove(e);
}

/// <summary>Mirrors Barter.js — system barter prices.</summary>
public sealed class BarterComponent
{
    public const int DealAmount=100, BatchSize=5, ConstantDiff=10;
    public const float DiffPerDeal=2f, DiffRestore=0.5f, RestoreIntervalMs=5000;
    public Dictionary<string,float> PriceDiffs=new();
    public int? RestoreTimer;

    public BarterComponent(string[]? resources=null)
    {
        foreach(var r in resources??new[]{"food","wood","stone","metal"})
            PriceDiffs[r]=0;
    }
    public Dictionary<string,(float buy,float sell)> GetPrices(Dictionary<string,(float buy,float sell)> mult, Dictionary<string,float> truePrices)
    {
        var ret=new Dictionary<string,(float,float)>();
        foreach(var kv in PriceDiffs)
        {
            float tp=truePrices.GetValueOrDefault(kv.Key,1);
            float buy= tp*(DealAmount+ConstantDiff+kv.Value)* mult.GetValueOrDefault(kv.Key,(1,1)).buy /DealAmount;
            float sell=tp*(DealAmount-ConstantDiff+kv.Value)* mult.GetValueOrDefault(kv.Key,(1,1)).sell/DealAmount;
            ret[kv.Key]=(buy,sell);
        }
        return ret;
    }
    public bool Exchange(string sell,string buy,int amount, System.Func<string,int,bool> canSubtract, System.Action<string,int> add, Dictionary<string,float> truePrices, Dictionary<string,(float buy,float sell)> mult)
    {
        if(amount!=DealAmount && amount!=BatchSize*DealAmount) return false;
        if(!PriceDiffs.ContainsKey(sell)||!PriceDiffs.ContainsKey(buy)) return false;
        if(!canSubtract(sell,amount)) return false;
        var prices=GetPrices(mult,truePrices);
        int gained=(int)MathF.Round(prices[sell].sell / prices[buy].buy * amount);
        add(buy,gained);
        float diff=DiffPerDeal*gained/DealAmount;
        float max=DealAmount*0.99f;
        PriceDiffs[sell]=Math.Clamp(PriceDiffs[sell]-diff, ConstantDiff-max, max-ConstantDiff);
        PriceDiffs[buy]=Math.Clamp(PriceDiffs[buy]+diff, ConstantDiff-max, max-ConstantDiff);
        return true;
    }
    public void ProgressTimeout()
    {
        bool need=false;
        foreach(var k in new List<string>(PriceDiffs.Keys))
        {
            PriceDiffs[k]-=Math.Clamp(PriceDiffs[k],-DiffRestore,DiffRestore);
            if(PriceDiffs[k]!=0) need=true;
        }
        if(!need) RestoreTimer=null;
    }
}
