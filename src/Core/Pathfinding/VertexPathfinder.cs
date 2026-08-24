/* Ported from source/simulation2/helpers/VertexPathfinder.h/.cpp — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/VertexPathfinder.cpp/.h — 0 A.D. GPL-2.0
/// Short-range precise pathfinder (visibility graph). Complements LongPathfinder.
/// Essential API: ComputePath / GetPath using Grid + obstruction squares.

using System;
using System.Collections.Generic;

namespace Rts0.Core;

public struct Vertex
{
    public FixedVector2D P;
    public Fixed G, H;
    public ushort Pred;
    public byte Status; // 0 unexplored,1 open,2 closed
    public byte QuadInward, QuadOutward;
}

public struct Edge { public FixedVector2D P0, P1; }
public struct Square { public FixedVector2D P0, P1; }
public struct EdgeAA { public FixedVector2D P0; public Fixed C1; }

public sealed class VertexPathfinder
{
    private const byte UNEXPLORED = 0, OPEN = 1, CLOSED = 2;
    private const byte Q_NONE = 0, Q_BL = 1, Q_TR = 2, Q_TL = 4, Q_BR = 8, Q_ALL = 15;
    private static readonly Fixed EdgeDelta = Fixed.FromInt(1) / 16;

    private readonly ushort _gridSize;
    private readonly Grid<ushort> _terrainGrid;

    public VertexPathfinder(ushort gridSize, Grid<ushort> terrainGrid) { _gridSize = gridSize; _terrainGrid = terrainGrid; }

    public WaypointPath ComputeShortPath(ShortPathRequest req, List<ObstructionSquare> squares)
    {
        Fixed rx0 = req.X0 - req.Range, rx1 = req.X0 + req.Range;
        Fixed rz0 = req.Z0 - req.Range, rz1 = req.Z0 + req.Range;
        // nudge toward goal if outside range (mirrors 0 A.D.)
        var toGoal = new FixedVector2D(req.Goal.X - req.X0, req.Goal.Z - req.Z0);
        if (toGoal.Length() >= req.Range && req.Range < Fixed.FromInt(46))
        {
            Fixed len = toGoal.Length();
            Fixed inv = Fixed.One / len;
            Fixed shift = toGoal.Length() / Fixed.FromInt(2) < req.Range * 3 / 5 ? toGoal.Length() / Fixed.FromInt(2) : req.Range * 3 / 5;
            var delta = toGoal.Multiply(shift).Multiply(inv);
            rx0 += delta.X; rx1 += delta.X; rz0 += delta.Y; rz1 += delta.Y;
        }

        var verts = new List<Vertex>();
        var edges = new List<Edge>();
        var squaresAA = new List<Square>();

        // domain boundary
        edges.Add(new Edge { P0 = new FixedVector2D(rx0, rz0), P1 = new FixedVector2D(rx0, rz1) });
        edges.Add(new Edge { P0 = new FixedVector2D(rx0, rz1), P1 = new FixedVector2D(rx1, rz1) });
        edges.Add(new Edge { P0 = new FixedVector2D(rx1, rz1), P1 = new FixedVector2D(rx1, rz0) });
        edges.Add(new Edge { P0 = new FixedVector2D(rx1, rz0), P1 = new FixedVector2D(rx0, rz0) });

        var startPos = new FixedVector2D(req.X0, req.Z0);
        Fixed hStart = (startPos - NearestOnGoal(startPos, req.Goal)).Length();
        verts.Add(new Vertex { P = startPos, G = Fixed.Zero, H = hStart, Status = OPEN, QuadOutward = Q_ALL });
        verts.Add(new Vertex { P = new FixedVector2D(req.Goal.X, req.Goal.Z), Status = UNEXPLORED, QuadOutward = Q_ALL });

        // obstruction verts + edges
        foreach (var sq in squares)
        {
            bool aa = sq.U.X == Fixed.One && sq.U.Y == Fixed.Zero && sq.V.X == Fixed.Zero && sq.V.Y == Fixed.One;
            Fixed clr = req.Clearance;
            var hd0 = new FixedVector2D(sq.Hw + clr + EdgeDelta, sq.Hh + clr + EdgeDelta);
            var hd1 = new FixedVector2D(sq.Hw + clr + EdgeDelta, Fixed.Zero - (sq.Hh + clr + EdgeDelta));
            FixedVector2D ctr = new(sq.X, sq.Z);
            Vertex v = new() { Status = UNEXPLORED, QuadOutward = Q_ALL };
            v.P = new FixedVector2D(ctr.X - hd0.Dot(sq.U), ctr.Y + hd0.Dot(sq.V));
            if (aa) { v.QuadInward = Q_BR; v.QuadOutward = (byte)(~Q_BR & 0xF); }
            if (v.P.X >= rx0 && v.P.X <= rx1 && v.P.Y >= rz0 && v.P.Y <= rz1) verts.Add(v);
            v.P = new FixedVector2D(ctr.X - hd1.Dot(sq.U), ctr.Y + hd1.Dot(sq.V));
            if (aa) { v.QuadInward = Q_TR; v.QuadOutward = (byte)(~Q_TR & 0xF); }
            if (v.P.X >= rx0 && v.P.X <= rx1 && v.P.Y >= rz0 && v.P.Y <= rz1) verts.Add(v);
            v.P = new FixedVector2D(ctr.X + hd0.Dot(sq.U), ctr.Y - hd0.Dot(sq.V));
            if (aa) { v.QuadInward = Q_TL; v.QuadOutward = (byte)(~Q_TL & 0xF); }
            if (v.P.X >= rx0 && v.P.X <= rx1 && v.P.Y >= rz0 && v.P.Y <= rz1) verts.Add(v);
            v.P = new FixedVector2D(ctr.X + hd1.Dot(sq.U), ctr.Y - hd1.Dot(sq.V));
            if (aa) { v.QuadInward = Q_BL; v.QuadOutward = (byte)(~Q_BL & 0xF); }
            if (v.P.X >= rx0 && v.P.X <= rx1 && v.P.Y >= rz0 && v.P.Y <= rz1) verts.Add(v);

            var h0 = new FixedVector2D(sq.Hw + clr, sq.Hh + clr);
            var h1 = new FixedVector2D(sq.Hw + clr, Fixed.Zero - (sq.Hh + clr));
            var ev0 = new FixedVector2D(ctr.X - h0.Dot(sq.U), ctr.Y + h0.Dot(sq.V));
            var ev1 = new FixedVector2D(ctr.X - h1.Dot(sq.U), ctr.Y + h1.Dot(sq.V));
            var ev2 = new FixedVector2D(ctr.X + h0.Dot(sq.U), ctr.Y - h0.Dot(sq.V));
            var ev3 = new FixedVector2D(ctr.X + h1.Dot(sq.U), ctr.Y - h1.Dot(sq.V));
            if (aa) squaresAA.Add(new Square { P0 = ev1, P1 = ev3 });
            else { edges.Add(new Edge { P0 = ev0, P1 = ev1 }); edges.Add(new Edge { P0 = ev1, P1 = ev2 }); edges.Add(new Edge { P0 = ev2, P1 = ev3 }); edges.Add(new Edge { P0 = ev3, P1 = ev0 }); }
        }

        // clip verts inside AA squares
        for (int si = 0; si < squaresAA.Count; si++)
        {
            var sq2 = squaresAA[si];
            if (startPos.X >= sq2.P0.X && startPos.Y >= sq2.P0.Y && startPos.X <= sq2.P1.X && startPos.Y <= sq2.P1.Y) continue;
            for (int vi = 2; vi < verts.Count; vi++)
            { var p = verts[vi].P; if (p.X >= sq2.P0.X && p.Y >= sq2.P0.Y && p.X <= sq2.P1.X && p.Y <= sq2.P1.Y) { var tmp = verts[vi]; tmp.Status = CLOSED; verts[vi] = tmp; } }
        }

        Fixed weight = edges.Count > 100 || squaresAA.Count > 100 ? Fixed.FromInt(5) / Fixed.FromInt(3) : Fixed.FromInt(4) / Fixed.FromInt(3);
        var open = new PriorityQueueHeap<ushort, Fixed, Fixed>();
        open.Push(new PriorityQueueItem<ushort, Fixed, Fixed> { Id = 0, Rank = verts[0].H, H = verts[0].H });
        ushort best = 0; Fixed hBest = hStart;

        while (!open.Empty())
        {
            var cur = open.Pop(); int cid = cur.Id;
            var cv = verts[cid]; cv.Status = CLOSED; verts[cid] = cv;
            if (cid == 1) { best = 1; break; }
            // split AA edges for speed
            var unaligned = new List<Edge>(); var left = new List<EdgeAA>(); var right = new List<EdgeAA>(); var bottom = new List<EdgeAA>(); var top = new List<EdgeAA>();
            SplitAA(verts[cid].P, edges, squaresAA, unaligned, left, right, bottom, top);
            for (int n = 0; n < verts.Count; n++)
            {
                if (verts[n].Status == CLOSED) continue;
                FixedVector2D npos = n == 1 ? Clamp(NearestOnGoal(verts[cid].P, req.Goal), rx0, rz0, rx1, rz1) : verts[n].P;
                byte quad = 0;
                if (verts[cid].P.X <= npos.X && verts[cid].P.Y <= npos.Y) quad |= Q_BL;
                if (verts[cid].P.X >= npos.X && verts[cid].P.Y >= npos.Y) quad |= Q_TR;
                if (verts[cid].P.X <= npos.X && verts[cid].P.Y >= npos.Y) quad |= Q_TL;
                if (verts[cid].P.X >= npos.X && verts[cid].P.Y <= npos.Y) quad |= Q_BR;
                if ((verts[cid].QuadOutward & quad) == 0 && cid != 0 && n != 1) continue;
                if (!Visible(verts[cid].P, npos, unaligned, left, right, bottom, top)) continue;
                Fixed g = verts[cid].G + (verts[cid].P - npos).Length();
                if (verts[n].Status == UNEXPLORED)
                {
                    var nv = verts[n]; nv.Status = OPEN; nv.G = g; nv.H = DistanceToGoal(npos, req.Goal).Multiply(weight); nv.Pred = (ushort)cid;
                    if (n == 1) nv.P = npos; verts[n] = nv;
                    open.Push(new PriorityQueueItem<ushort, Fixed, Fixed> { Id = (ushort)n, Rank = g + nv.H, H = nv.H });
                    if (nv.H < hBest) { hBest = nv.H; best = (ushort)n; }
                }
                else if (g < verts[n].G)
                {
                    var nv = verts[n]; Fixed prev = nv.G + nv.H; nv.G = g; nv.Pred = (ushort)cid; if (n == 1) nv.P = npos; verts[n] = nv;
                    open.Promote((ushort)n, prev, g + nv.H, nv.H);
                }
            }
        }

        var path = new WaypointPath();
        for (ushort id = best; id != 0; id = verts[id].Pred) path.Waypoints.Add(new Waypoint(verts[id].P.X, verts[id].P.Y));
        path.Waypoints.Reverse();
        return path;
    }

    // Convenience wrapper matching LongPathfinder style
    public List<(Fixed x, Fixed z)> ComputePath(Fixed x0, Fixed z0, PathGoal goal, Fixed clearance, Fixed range, ushort passClass, List<ObstructionSquare> obs)
    {
        var req = new ShortPathRequest { X0 = x0, Z0 = z0, Goal = goal, Clearance = clearance, Range = range, PassClass = passClass };
        var wp = ComputeShortPath(req, obs);
        var ret = new List<(Fixed, Fixed)>(); foreach (var w in wp.Waypoints) ret.Add((w.X, w.Z)); return ret;
    }

    public IReadOnlyList<Waypoint> GetPath(WaypointPath p) => p.Waypoints;

    private static FixedVector2D NearestOnGoal(FixedVector2D pos, PathGoal goal)
    {
        // simplified: use goal center + half extents square
        return new FixedVector2D(goal.X, goal.Z);
    }
    private static Fixed DistanceToGoal(FixedVector2D pos, PathGoal goal) => goal.DistanceToPoint(pos);
    private static FixedVector2D Clamp(FixedVector2D p, Fixed x0, Fixed z0, Fixed x1, Fixed z1)
        => new(MathUtil.Clamp(p.X, x0, x1), MathUtil.Clamp(p.Y, z0, z1));

    private static bool Visible(FixedVector2D a, FixedVector2D b, List<Edge> unaligned, List<EdgeAA> left, List<EdgeAA> right, List<EdgeAA> bottom, List<EdgeAA> top)
        => CheckLeft(a, b, left) && CheckRight(a, b, right) && CheckBottom(a, b, bottom) && CheckTop(a, b, top) && CheckUnaligned(a, b, unaligned);

    private static bool CheckUnaligned(FixedVector2D a, FixedVector2D b, List<Edge> edges)
    {
        var abn = new FixedVector2D(b.Y - a.Y, Fixed.Zero - (b.X - a.X));
        foreach (var e in edges)
        {
            var d = new FixedVector2D(e.P1.Y - e.P0.Y, Fixed.Zero - (e.P1.X - e.P0.X));
            if ((a - e.P0).Dot(d) < Fixed.Zero) continue;
            if ((b - e.P0).Dot(d) > Fixed.Zero) continue;
            if ((e.P0 - a).Dot(abn) > Fixed.Zero) continue;
            if ((e.P1 - a).Dot(abn) < Fixed.Zero) continue;
            return false;
        }
        return true;
    }
    private static bool CheckLeft(FixedVector2D a, FixedVector2D b, List<EdgeAA> e) { if (a.X >= b.X) return true; var abn = new FixedVector2D(b.Y - a.Y, Fixed.Zero - (b.X - a.X)); foreach (var ed in e) { if (b.X < ed.P0.X) continue; var p0 = new FixedVector2D(ed.P0.X, ed.C1); var p1 = ed.P0; if ((p0 - a).Dot(abn) > Fixed.Zero) continue; if ((p1 - a).Dot(abn) < Fixed.Zero) continue; return false; } return true; }
    private static bool CheckRight(FixedVector2D a, FixedVector2D b, List<EdgeAA> e) { if (a.X <= b.X) return true; var abn = new FixedVector2D(b.Y - a.Y, Fixed.Zero - (b.X - a.X)); foreach (var ed in e) { if (b.X > ed.P0.X) continue; var p0 = new FixedVector2D(ed.P0.X, ed.C1); var p1 = ed.P0; if ((p0 - a).Dot(abn) > Fixed.Zero) continue; if ((p1 - a).Dot(abn) < Fixed.Zero) continue; return false; } return true; }
    private static bool CheckBottom(FixedVector2D a, FixedVector2D b, List<EdgeAA> e) { if (a.Y >= b.Y) return true; var abn = new FixedVector2D(b.Y - a.Y, Fixed.Zero - (b.X - a.X)); foreach (var ed in e) { if (b.Y < ed.P0.Y) continue; var p0 = ed.P0; var p1 = new FixedVector2D(ed.C1, ed.P0.Y); if ((p0 - a).Dot(abn) > Fixed.Zero) continue; if ((p1 - a).Dot(abn) < Fixed.Zero) continue; return false; } return true; }
    private static bool CheckTop(FixedVector2D a, FixedVector2D b, List<EdgeAA> e) { if (a.Y <= b.Y) return true; var abn = new FixedVector2D(b.Y - a.Y, Fixed.Zero - (b.X - a.X)); foreach (var ed in e) { if (b.Y > ed.P0.Y) continue; var p0 = ed.P0; var p1 = new FixedVector2D(ed.C1, ed.P0.Y); if ((p0 - a).Dot(abn) > Fixed.Zero) continue; if ((p1 - a).Dot(abn) < Fixed.Zero) continue; return false; } return true; }

    private static void SplitAA(FixedVector2D a, List<Edge> edges, List<Square> squares, List<Edge> unaligned, List<EdgeAA> left, List<EdgeAA> right, List<EdgeAA> bottom, List<EdgeAA> top)
    {
        foreach (var s in squares)
        {
            if (a.X <= s.P0.X) left.Add(new EdgeAA { P0 = s.P0, C1 = s.P1.Y });
            if (a.X >= s.P1.X) right.Add(new EdgeAA { P0 = s.P1, C1 = s.P0.Y });
            if (a.Y <= s.P0.Y) bottom.Add(new EdgeAA { P0 = s.P0, C1 = s.P1.X });
            if (a.Y >= s.P1.Y) top.Add(new EdgeAA { P0 = s.P1, C1 = s.P0.X });
        }
        foreach (var e in edges)
        {
            if (e.P0.X == e.P1.X)
            {
                if (e.P1.Y < e.P0.Y) { if (a.X <= e.P0.X) left.Add(new EdgeAA { P0 = e.P1, C1 = e.P0.Y }); }
                else { if (a.X >= e.P0.X) right.Add(new EdgeAA { P0 = e.P1, C1 = e.P0.Y }); }
            }
            else if (e.P0.Y == e.P1.Y)
            {
                if (e.P0.X < e.P1.X) { if (a.Y <= e.P0.Y) bottom.Add(new EdgeAA { P0 = e.P0, C1 = e.P1.X }); }
                else { if (a.Y >= e.P0.Y) top.Add(new EdgeAA { P0 = e.P0, C1 = e.P1.X }); }
            }
            else unaligned.Add(e);
        }
    }
}
