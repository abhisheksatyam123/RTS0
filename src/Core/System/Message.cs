/* Ported from source/simulation2/system/Message.h — 0 A.D. GPL-2.0 */
/// <summary>Message types — mirrors CMessage (source/simulation2/system/Message.h).</summary>
/// <remarks>Reference: source/simulation2/system/Message.h, MessageTypeId from TypeList.h</remarks>
using System;

namespace Rts0.Core;

/// <summary>Base simulation message. Mirrors CMessage (GetType, GetScriptHandlerName).</summary>
public abstract class CMessage
{
    public abstract int GetTypeId();
    public abstract string GetScriptHandlerName();
    public abstract string GetScriptGlobalHandlerName();
    public virtual object? ToJSVal() => null;
}

/// <summary>Well-known message type IDs (subset of 0 A.D. TypeList.h MT_*). Extend as needed.</summary>
public static class MessageTypeId
{
    public const int Invalid = 0;
    public const int Create = 1;
    public const int Destroy = 2;
    public const int Update = 3;
    public const int UpdateMotionUnit = 4;
    public const int OwnershipChanged = 5;
    public const int TerritoryChanged = 6;
    public const int ValueModification = 7;
    public const int InterpolatedUpdate = 8;
    public const int RenderSubmit = 9;
}

/// <summary>Scripted message — mirrors CMessageScripted in ComponentManager.cpp.</summary>
public sealed class CMessageScripted : CMessage
{
    private readonly int _mtid;
    private readonly string _name;
    private readonly object? _data;
    public CMessageScripted(int mtid, string name, object? data) { _mtid = mtid; _name = name; _data = data; }
    public override int GetTypeId() => _mtid;
    public override string GetScriptHandlerName() => "On" + _name;
    public override string GetScriptGlobalHandlerName() => "OnGlobal" + _name;
    public override object? ToJSVal() => _data;
}

public sealed class CMessageCreate : CMessage
{
    public uint EntityId { get; }
    public CMessageCreate(uint id) => EntityId = id;
    public override int GetTypeId() => MessageTypeId.Create;
    public override string GetScriptHandlerName() => "OnCreate";
    public override string GetScriptGlobalHandlerName() => "OnGlobalCreate";
}

public sealed class CMessageDestroy : CMessage
{
    public uint EntityId { get; }
    public CMessageDestroy(uint id) => EntityId = id;
    public override int GetTypeId() => MessageTypeId.Destroy;
    public override string GetScriptHandlerName() => "OnDestroy";
    public override string GetScriptGlobalHandlerName() => "OnGlobalDestroy";
}

public sealed class CMessageUpdate : CMessage
{
    public float TurnLength { get; }
    public CMessageUpdate(float len) => TurnLength = len;
    public override int GetTypeId() => MessageTypeId.Update;
    public override string GetScriptHandlerName() => "OnUpdate";
    public override string GetScriptGlobalHandlerName() => "OnGlobalUpdate";
}
