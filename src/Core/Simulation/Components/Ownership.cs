/* Ported from source/simulation2/components/CCmpOwnership.cpp + ICmpOwnership.h — 0 A.D. GPL-2.0 */
/// <summary>Player ownership — mirrors ICmpOwnership / CCmpOwnership.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class Ownership : IComponent
{
    public int EntityId { get; private set; }
    private int _owner = Player.InvalidPlayer;
    public event Action<int,int,int> OwnershipChanged; // entity, old, new

    public Ownership(int entityId) => EntityId = entityId;
    public Ownership(int entityId, int owner) { EntityId = entityId; _owner = owner; }

    public void Init(Dictionary<string, object> p) => _owner = Player.InvalidPlayer;
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) => w.Write(_owner);
    public void Deserialize(System.IO.BinaryReader r) => _owner = r.ReadInt32();

    public int GetOwner() => _owner;

    public void SetOwner(int playerId)
    {
        if (playerId == _owner) return;
        int old = _owner;
        _owner = playerId;
        OwnershipChanged?.Invoke(EntityId, old, playerId);
    }

    public void SetOwnerQuiet(int playerId)
    {
        if (playerId != _owner) _owner = playerId;
    }

    public bool IsOwnedBy(int player) => _owner == player;
    public bool IsUnassigned => _owner == Player.InvalidPlayer;
    public bool IsGaia => _owner == Player.Gaia;

    // helper for GameSim integration
    public void ApplyToSimUnit(SimUnit u) => u.Team = _owner == 1 ? Team.Enemy : Team.Player;
}
