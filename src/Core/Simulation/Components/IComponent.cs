/* Ported from source/simulation2/system/Component.h + Interface.h — 0 A.D. GPL-2.0 */
/// <summary>Base component interface. Ported from 0 A.D. IComponent / ComponentManager.</summary>
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Minimal IComponent — every simulation component implements this.</summary>
public interface IComponent
{
    int EntityId { get; }
    void Init(Dictionary<string, object> paramNode);
    void Deinit();
    void Serialize(System.IO.BinaryWriter writer);
    void Deserialize(System.IO.BinaryReader reader);
}

/// <summary>System component (singleton) marker — mirrors CCmp* with SYSTEM_ENTITY.</summary>
public interface ISystemComponent : IComponent { }

/// <summary>Component type identifier — mirrors ComponentTypeId.</summary>
public enum ComponentTypeId
{
    Position = 1, Ownership, Vision, UnitMotion, Pathfinder,
    ObstructionManager, RangeManager, TerritoryManager,
    Health, Attack, ResourceSupply
}
