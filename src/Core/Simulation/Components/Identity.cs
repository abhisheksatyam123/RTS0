/// Ported from /source/mods/public/simulation/components/Identity.js — 0 A.D. GPL-2.0
using System.Collections.Generic;
using System.Linq;

namespace Rts0.Core;

/// <summary>Mirrors Identity.js — civ, classes, selection grouping.</summary>
public sealed class IdentityComponent
{
    public string Civ="athen";
    public string Lang="greek";
    public string GenericName="";
    public string SpecificName="";
    public string Icon="";
    public string Rank=""; // Basic|Advanced|Elite
    public string Phenotype="default";
    public bool Controllable=true;
    public bool Undeletable;
    public string SelectionGroupName="";
    public List<string> Classes=new();
    public List<string> VisibleClasses=new();

    public IdentityComponent(string civ="athen",string generic="",IEnumerable<string>? classes=null)
    {
        Civ=civ; GenericName=generic;
        if(classes!=null) Classes.AddRange(classes);
    }
    public string GetCiv()=>Civ;
    public string GetLang()=>Lang;
    public string GetRank()=>Rank;
    public string GetPhenotype()=>Phenotype;
    public IReadOnlyList<string> GetClassesList()=>Classes;
    public IReadOnlyList<string> GetVisibleClassesList()=>VisibleClasses;
    public bool HasClass(string c)=>Classes.Contains(c);
    public string GetGenericName()=>GenericName;
    public string GetSelectionGroupName()=>SelectionGroupName;
    public bool IsControllable()=>Controllable;
    public bool IsUndeletable()=>Undeletable;
    public bool SetPhenotype(string p, IReadOnlyList<string>? possible=null)
    {
        if(possible!=null && !possible.Contains(p)) return false;
        if(Phenotype==p) return false;
        Phenotype=p; return true;
    }
}
