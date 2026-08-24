using System;
using System.IO;
using Godot;

namespace Rts0.Core;

public enum CommandType : byte
{
    Move = 1,
    BuildHouse = 2,
    Gather = 3,
    Attack = 4,
}

// Every player action is a serializable command -> host-authoritative sync,
// replays and (if ever needed) deterministic lockstep all hook in here.
public abstract class GameCommand
{
    public byte PlayerId;

    public abstract CommandType Type { get; }

    protected abstract void WritePayload(BinaryWriter writer);
    protected abstract void ReadPayload(BinaryReader reader);

    public void Encode(BinaryWriter writer)
    {
        writer.Write(PlayerId);
        writer.Write((byte)Type);
        WritePayload(writer);
    }

    public static GameCommand Decode(BinaryReader reader)
    {
        byte playerId = reader.ReadByte();
        CommandType type = (CommandType)reader.ReadByte();
        GameCommand cmd = type switch
        {
            CommandType.Move => new MoveCommand(),
            CommandType.BuildHouse => new BuildHouseCommand(),
            CommandType.Gather => new GatherCommand(),
            CommandType.Attack => new AttackCommand(),
            _ => throw new InvalidDataException($"Unknown command type: {type}"),
        };
        cmd.PlayerId = playerId;
        cmd.ReadPayload(reader);
        return cmd;
    }
}

public sealed class MoveCommand : GameCommand
{
    public int[] UnitIds = Array.Empty<int>();
    public Vector3 Target;

    public override CommandType Type => CommandType.Move;

    protected override void WritePayload(BinaryWriter writer)
    {
        writer.Write(UnitIds.Length);
        foreach (int id in UnitIds) writer.Write(id);
        writer.Write(Target.X);
        writer.Write(Target.Y);
        writer.Write(Target.Z);
    }

    protected override void ReadPayload(BinaryReader reader)
    {
        int count = reader.ReadInt32();
        UnitIds = new int[count];
        for (int i = 0; i < count; i++) UnitIds[i] = reader.ReadInt32();
        Target = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
    }
}

public sealed class BuildHouseCommand : GameCommand
{
    public Vector2Int Cell;

    public override CommandType Type => CommandType.BuildHouse;

    protected override void WritePayload(BinaryWriter writer)
    {
        writer.Write(Cell.X);
        writer.Write(Cell.Y);
    }

    protected override void ReadPayload(BinaryReader reader)
    {
        Cell = new Vector2Int(reader.ReadInt32(), reader.ReadInt32());
    }
}

public sealed class GatherCommand : GameCommand
{
    public int ResourceId;
    public override CommandType Type => CommandType.Gather;
    protected override void WritePayload(BinaryWriter w) => w.Write(ResourceId);
    protected override void ReadPayload(BinaryReader r) => ResourceId = r.ReadInt32();
}

public sealed class AttackCommand : GameCommand
{
    public int TargetId;
    public override CommandType Type => CommandType.Attack;
    protected override void WritePayload(BinaryWriter w) => w.Write(TargetId);
    protected override void ReadPayload(BinaryReader r) => TargetId = r.ReadInt32();
}
