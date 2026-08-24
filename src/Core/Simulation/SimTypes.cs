using System.Collections.Generic;
using Godot;

namespace Rts0.Core;

public enum Team : byte { Player = 0, Enemy = 1 }

public enum ResourceType : byte { Tree = 0, GoldVein = 1 }

// Plain data only: no Godot Nodes in the sim, so it can run anywhere and stay deterministic-friendly.
// Ported fields mirror 0 A.D. CCmpUnitMotion / Health / ResourceGatherer.
// SimUnit now holds Position + Ownership + Vision components (see src/Core/Simulation/Components).
public sealed class SimUnit
{
    public int Id;
    public Team Team = Team.Player;
    public Vector3 Pos;
    public Vector3 PrevPos;   // for render interpolation between fixed ticks
    public Vector3 Target;
    public bool HasOrder;
    public Position PositionComp;   // deterministic FixedVector2D mirror of Pos — see CCmpPosition
    public Ownership OwnershipComp;
    public Vision VisionComp;
    public UnitMotion MotionComp;

    // 0 A.D. UnitAI / Health / Attack fields
    public int Hp = 100;
    public int MaxHp = 100;
    public float AttackRange = 1.5f;
    public int AttackDamage = 12;
    public float AttackCooldown = 1.0f;
    public float AttackTimer;
    public int Carry;
    public int CarryCapacity = 20;

    // Path assigned by GameSim.AssignPath (LongPathfinder output)
    public List<Vector3> Path;
    public int PathIndex;
}

public sealed class SimBuilding
{
    public int Id;
    public Team Team = Team.Player;
    public Vector2I AnchorCell;   // top-left footprint cell
    public int Footprint;           // cells per side
    public float SizeMeters;
    public bool IsCenter;
    public int Hp = 200;
    public int MaxHp = 200;
    public Position PositionComp;
    public Ownership OwnershipComp;

    public Vector3 BasePos => new(AnchorCell.X * GameSim.Grid, 0.0f, AnchorCell.Y * GameSim.Grid);
    public Vector3 CenterPos => BasePos + new Vector3(SizeMeters * 0.5f, 0, SizeMeters * 0.5f);
}

public sealed class SimResource
{
    public int Id;
    public ResourceType Type;
    public Vector2I Cell;
    public int Amount = 200;
    public Vector3 Pos => new((Cell.X + 0.5f) * GameSim.Grid, 0, (Cell.Y + 0.5f) * GameSim.Grid);
}
