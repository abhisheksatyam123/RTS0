using Godot;

namespace Rts0.Core;

// Plain data only: no Godot Nodes in the sim, so it can run anywhere and stay deterministic-friendly.
public sealed class SimUnit
{
    public int Id;
    public Vector3 Pos;
    public Vector3 PrevPos;   // for render interpolation between fixed ticks
    public Vector3 Target;
    public bool HasOrder;
}

public sealed class SimBuilding
{
    public int Id;
    public Vector2Int AnchorCell;   // top-left footprint cell
    public int Footprint;           // cells per side
    public float SizeMeters;
    public bool IsCenter;

    public Vector3 BasePos => new(AnchorCell.X * GameSim.Grid, 0.0f, AnchorCell.Y * GameSim.Grid);
}
