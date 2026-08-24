/* Ported from source/simulation2/helpers/PathGoal.h/.cpp — 0 A.D. GPL-2.0 */

using System;

namespace Rts0.Core;

public sealed class PathGoal
{
    public enum GoalType { Point, Circle, InvertedCircle, Square, InvertedSquare }
    public GoalType Type;
    public Fixed X, Z;
    public Fixed HalfWidth, HalfHeight;
    public Fixed MaxDist;

    public static PathGoal Point(Fixed x, Fixed z) => new() { Type = GoalType.Point, X = x, Z = z };
    public static PathGoal Circle(Fixed x, Fixed z, Fixed radius) => new() { Type = GoalType.Circle, X = x, Z = z, HalfWidth = radius };

    public bool Contains(int i, int j, Fixed cellSize)
    {
        FixedVector2D p = new(Fixed.FromInt(i) * Fixed.FromFloat(cellSize.ToFloat()) + Fixed.FromFloat(cellSize.ToFloat()/2),
                              Fixed.FromInt(j) * Fixed.FromFloat(cellSize.ToFloat()) + Fixed.FromFloat(cellSize.ToFloat()/2));
        return DistanceToPoint(p).CompareTo(Fixed.Zero) <= 0;
    }

    public Fixed DistanceToPoint(FixedVector2D pos)
    {
        FixedVector2D center = new(X, Z);
        FixedVector2D d = pos - center;
        Fixed len = d.Length();
        if (Type == GoalType.Point) return len;
        if (Type == GoalType.Circle) return len - HalfWidth;
        if (Type == GoalType.Square)
        {
            Fixed dx = (d.X.Absolute() - HalfWidth);
            Fixed dy = (d.Y.Absolute() - HalfHeight);
            if (dx < Fixed.Zero) dx = Fixed.Zero;
            if (dy < Fixed.Zero) dy = Fixed.Zero;
            return new FixedVector2D(dx, dy).Length();
        }
        return len;
    }
}
