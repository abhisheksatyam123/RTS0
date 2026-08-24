/// Ported from /source/mods/public/simulation/components/Auras.js — 0 A.D. GPL-2.0
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

public enum AuraType { Range, Formation, Garrison, GarrisonedUnits, TurretedUnits, Global, Player }

public sealed class AuraDef
{
    public string Name="";
    public AuraType Type;
    public float Radius;
    public string Affects=""; // class tokens
    public Dictionary<string,float> Modifications=new();
    public string RequiredTech="";
    public bool Stackable;
}

public sealed class AurasComponent
{
    public List<AuraDef> Auras = new();
    // auraName -> affected entity ids
    private readonly Dictionary<string, HashSet<int>> _targets = new();
    private readonly Dictionary<string, List<int>> _affectedPlayers = new();

    public IEnumerable<string> GetAuraNames()=>Auras.Select(a=>a.Name);
    public AuraDef? Get(string n)=>Auras.Find(a=>a.Name==n);
    public bool HasFormationAura()=>Auras.Any(a=>a.Type==AuraType.Formation);
    public bool HasGarrisonAura()=>Auras.Any(a=>a.Type==AuraType.Garrison);

    public IReadOnlyCollection<int> GetAffected(string name)
        => _targets.TryGetValue(name,out var s)? s : System.Array.Empty<int>();

    public void Clean(Dictionary<int,HashSet<string>> classByEnt, System.Func<string,bool> techResearched)
    {
        _targets.Clear();
        foreach(var a in Auras)
        {
            _targets[a.Name]=new HashSet<int>();
            if(!string.IsNullOrEmpty(a.RequiredTech) && !techResearched(a.RequiredTech)) continue;
            if(a.Type==AuraType.Range || a.Type==AuraType.Global) _affectedPlayers[a.Name]=new List<int>{0};
        }
    }
    public void ApplyAura(string name, IEnumerable<int> ents, Dictionary<int,HashSet<string>> classByEnt, System.Action<int,Dictionary<string,float>> addMod)
    {
        var def=Get(name); if(def==null) return;
        if(!_targets.TryGetValue(name,out var set)) _targets[name]=set=new HashSet<int>();
        foreach(var e in ents)
        {
            if(!classByEnt.TryGetValue(e,out var cls)) continue;
            if(!Matches(cls, def.Affects)) continue;
            if(set.Add(e) && def.Modifications.Count>0) addMod(e, def.Modifications);
        }
    }
    public void RemoveAura(string name, IEnumerable<int> ents, System.Action<int,Dictionary<string,float>> remMod)
    {
        var def=Get(name); if(def==null) return;
        if(!_targets.TryGetValue(name,out var set)) return;
        foreach(var e in ents) if(set.Remove(e) && def.Modifications.Count>0) remMod(e, def.Modifications);
    }
    public void ApplyFormation(IEnumerable<int> members, Dictionary<int,HashSet<string>> classByEnt, System.Action<int,Dictionary<string,float>> add)
    { foreach(var a in Auras.Where(x=>x.Type==AuraType.Formation)) ApplyAura(a.Name,members,classByEnt,add); }
    public void RemoveFormation(IEnumerable<int> members, System.Action<int,Dictionary<string,float>> rem)
    { foreach(var a in Auras.Where(x=>x.Type==AuraType.Formation)) RemoveAura(a.Name,members,rem); }

    private static bool Matches(HashSet<string> classes,string filter)
    {
        if(string.IsNullOrWhiteSpace(filter)) return true;
        foreach(var c in filter.Split(' ')) if(classes.Contains(c)) return true;
        return false;
    }
}
