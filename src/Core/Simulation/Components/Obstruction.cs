/* Ported from source/simulation2/components/CCmpObstruction* — 0 A.D. */

using Godot;

namespace Rts0.Core;

/// <summary> Marks a square obstruction on the Grid — mirrors ObstructionSquare in 0 A.D. </summary>
public sealed class ObstructionComponent
{
    public Vector2I Size; // in navcells
    public bool PassableByDefault = false;

    public ObstructionComponent(int w, int h) => Size = new Vector2I(w, h);

    public void ApplyToGrid(Grid<byte> grid, Vector2Int anchor, bool block)
    {
        byte val = block ? (byte)0 : (byte)1;
        for (int y = 0; y < Size.Y; y++)
        for (int x = 0; x < Size.X; x++)
        {
            int gx = anchor.X + x;
            int gy = anchor.Y + y;
            if (gx >= 0 && gy >= 0 && gx < grid.Width && gy < grid.Height)
                grid.Set(gx, gy, val);
        }
    }

    public static Vector2Int WorldToObstructionCells(Vector3 worldPos, float cellSize = 2.0f)
        => new(Mathf.FloorToInt(worldPos.X / cellSize), Mathf.FloorToInt(worldPos.Z / cellSize));
}
