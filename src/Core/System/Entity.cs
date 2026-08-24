/* Ported from source/simulation2/system/Entity.h — 0 A.D. GPL-2.0 */
/// <summary>Entity ID types — mirrors Entity.h (source/simulation2/system/Entity.h).</summary>
/// <remarks>Reference: source/simulation2/system/Entity.h (entity_id_t, INVALID_ENTITY, SYSTEM_ENTITY, CEntityHandle)</remarks>
using System;

namespace Rts0.Core;

/// <summary>Entity ID constants and helpers. Mirrors entity_id_t typedef and ENTITY_IS_* macros.</summary>
public static class Entity
{
    public const uint Invalid = 0;
    public const uint SystemEntity = 1;
    public const uint TagMask = 1u << 29;
    public const uint FirstLocal = TagMask;

    public static bool IsNormal(uint id) => (id & TagMask) == 0;
    public static bool IsLocal(uint id) => (id & TagMask) == TagMask;
    public static bool IsValid(uint id) => id != Invalid;
}

/// <summary>Handle wrapping an entity ID plus component cache pointer. Mirrors CEntityHandle.</summary>
public readonly struct EntityHandle : IEquatable<EntityHandle>
{
    public static readonly EntityHandle Invalid = new(Entity.Invalid, null);
    public uint Id { get; }
    internal object? Cache { get; }
    public bool IsValid => Id != Entity.Invalid && Cache != null;

    public EntityHandle(uint id, object? cache)
    {
        Id = id;
        Cache = cache;
    }
    public uint GetId() => Id;
    public object? GetComponentCache() => Cache;
    public bool Equals(EntityHandle other) => Id == other.Id;
    public override bool Equals(object? obj) => obj is EntityHandle h && Equals(h);
    public override int GetHashCode() => (int)Id;
    public static bool operator ==(EntityHandle a, EntityHandle b) => a.Id == b.Id;
    public static bool operator !=(EntityHandle a, EntityHandle b) => a.Id != b.Id;
    public override string ToString() => $"EntityHandle({Id})";
}

/// <summary>Minimal SEntityComponentCache — variable-length array of IComponent per InterfaceId.</summary>
public sealed class EntityComponentCache
{
    public int NumInterfaces { get; }
    public IComponent?[] Interfaces { get; }
    public EntityComponentCache(int numInterfaces)
    {
        NumInterfaces = numInterfaces;
        Interfaces = new IComponent?[numInterfaces];
    }
}
