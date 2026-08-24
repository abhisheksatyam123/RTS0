/* Ported from source/simulation2/helpers/Player.h — 0 A.D. GPL-2.0 */
/// <summary>Player identifier helpers — mirrors Player.h player_id_t + INVALID_PLAYER.</summary>
namespace Rts0.Core;

/// <summary>Player ID type — wraps int32 like 0 A.D. player_id_t.</summary>
public static class Player
{
    public const int InvalidPlayer = -1;
    public const int Gaia = 0;
    public const int MaxPlayers = 16;

    public static bool IsValid(int id) => id >= 0 && id <= MaxPlayers;
    public static bool IsGaia(int id) => id == Gaia;
    public static bool IsUnassigned(int id) => id == InvalidPlayer;
}

/// <summary>Typed wrapper for entity IDs — mirrors entity_id_t.</summary>
public readonly struct EntityId : System.IEquatable<EntityId>
{
    public readonly int Value;
    public EntityId(int v) => Value = v;
    public static implicit operator int(EntityId id) => id.Value;
    public static implicit operator EntityId(int v) => new(v);
    public bool Equals(EntityId other) => Value == other.Value;
    public override bool Equals(object obj) => obj is EntityId e && Equals(e);
    public override int GetHashCode() => Value;
    public static bool operator ==(EntityId a, EntityId b) => a.Value == b.Value;
    public static bool operator !=(EntityId a, EntityId b) => a.Value != b.Value;
    public override string ToString() => Value.ToString();
    public static readonly EntityId Invalid = new(Player.InvalidPlayer);
}
