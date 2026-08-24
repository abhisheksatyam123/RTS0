/// Ported from mods/public/simulation/components/Sound.js — 0 A.D. GPL-2.0
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Mirrors Sound.js — sound group lookup + play.</summary>
public sealed class SoundComponent
{
    public Dictionary<string,string> SoundGroups=new();

    public SoundComponent(Dictionary<string,string>? groups=null)
    { if(groups!=null) SoundGroups=new Dictionary<string,string>(groups); }

    public string GetSoundGroup(string name)
        => SoundGroups.TryGetValue(name,out var v)? v : "";

    public bool PlaySoundGroup(string name,string lang,string phenotype, System.Action<string,int>? play=null,int entity=-1)
    {
        if(!SoundGroups.TryGetValue(name,out var tmpl)) return false;
        string sound=tmpl.Replace("{lang}",lang).Replace("{phenotype}",phenotype);
        play?.Invoke(sound,entity);
        return true;
    }
    public IEnumerable<string> GetGroups()=>SoundGroups.Keys;
}
