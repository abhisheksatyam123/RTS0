/* Copyright (C) 2025 Wildfire Games.
 * Ported from source/simulation2/helpers/PathGoal.h/.cpp,
 * source/simulation2/helpers/HierarchicalPathfinder.h/.cpp (353+865 lines),
 * and source/simulation2/helpers/LongPathfinder.h/.cpp (1170 lines).
 * 0 A.D. - GPLv2. Adapted for Godot 4.3 C# (RTS0) with deterministic Fixed math.
 * Thronefall visuals unchanged; this file is simulation-only.
 */

using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Port of 0 A.D. PathGoal — point / circle / square goal shapes.</summary>
public sealed class PathGoal
{
    public enum GoalType { Point, Circle, InvertedCircle, Square, InvertedSquare }
    public GoalType Type;
    public Fixed X, Z;
    public Fixed HalfWidth, HalfHeight; // radius for circle
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

/// <summary>
/// Simplified hierarchical pathfinder — direct adaptation of 0 A.D. HierarchicalPathfinder.
/// Splits the nav grid into CHUNK_SIZE chunks, flood-fills passable cells into regions,
/// and builds a region adjacency graph to answer "can A reach B?" in O(1).
/// Full 0 A.D. uses CHUNK_SIZE=96 for 512x512 maps; RTS0 uses 8 for our 30x30 map
/// so the behavior is identical at smaller scale.
/// </summary>
public sealed class HierarchicalPathfinder
{
    public const int ChunkSize = 8;

    private struct RegionId : IEquatable<RegionId>, IComparable<RegionId>
    {
        public byte Ci, Cj;
        public ushort R;
        public int CompareTo(RegionId other)
        {
            int c = Ci.CompareTo(other.Ci);
            if (c != 0) return c;
            c = Cj.CompareTo(other.Cj);
            if (c != 0) return c;
            return R.CompareTo(other.R);
        }
        public bool Equals(RegionId other) => Ci == other.Ci && Cj == other.Cj && R == other.R;
    }

    private readonly int _w, _h, _chunksW, _chunksH;
    private readonly ushort[,] _regionGrid;
    private readonly Dictionary<RegionId, HashSet<RegionId>> _edges = new();
    private readonly Dictionary<RegionId, int> _globalRegion = new();
    private int _nextGlobalId = 1;

    public HierarchicalPathfinder(int w, int h)
    {
        _w = w; _h = h;
        _chunksW = (w + ChunkSize - 1) / ChunkSize;
        _chunksH = (h + ChunkSize - 1) / ChunkSize;
        _regionGrid = new ushort[w, h];
    }

    public void Recompute(Grid<byte> passability)
    {
        Array.Clear(_regionGrid, 0, _regionGrid.Length);
        _edges.Clear(); _globalRegion.Clear(); _nextGlobalId = 1;
        ushort nextLocal = 1;
        // Flood-fill per chunk
        for (int cj = 0; cj < _chunksH; cj++)
        for (int ci = 0; ci < _chunksW; ci++)
        {
            for (int y = cj * ChunkSize; y < Math.Min((cj + 1) * ChunkSize, _h); y++)
            for (int x = ci * ChunkSize; x < Math.Min((ci + 1) * ChunkSize, _w); x++)
            {
                if (passability.Get(x, y) == 0 || _regionGrid[x, y] != 0) continue;
                FloodFill(passability, ci, cj, x, y, nextLocal++);
            }
        }
        BuildEdges(passability);
        BuildGlobalRegions();
    }

    private void FloodFill(Grid<byte> pass, int ci, int cj, int sx, int sy, ushort id)
    {
        var q = new Queue<(int,int)>();
        q.Enqueue((sx,sy)); _regionGrid[sx,sy]=id;
        int[] dx={1,-1,0,0}, dy={0,0,1,-1};
        while(q.Count>0)
        {
            var (x,y)=q.Dequeue();
            for(int k=0;k<4;k++)
            {
                int nx=x+dx[k], ny=y+dy[k];
                if(nx < ci*ChunkSize || nx >= Math.Min((ci+1)*ChunkSize,_w)) continue;
                if(ny < cj*ChunkSize || ny >= Math.Min((cj+1)*ChunkSize,_h)) continue;
                if(pass.Get(nx,ny)==0 || _regionGrid[nx,ny]!=0) continue;
                _regionGrid[nx,ny]=id; q.Enqueue((nx,ny));
            }
        }
    }

    private void BuildEdges(Grid<byte> pass)
    {
        // Edges only between adjacent chunks (as in 0 A.D.)
        for(int y=0;y<_h;y++) for(int x=0;x<_w-1;x++)
        {
            if(pass.Get(x,y)==0 || pass.Get(x+1,y)==0) continue;
            if(ChunkOf(x)!=ChunkOf(x+1))
                AddEdge(x,y,x+1,y);
        }
        for(int y=0;y<_h-1;y++) for(int x=0;x<_w;x++)
        {
            if(pass.Get(x,y)==0 || pass.Get(x,y+1)==0) continue;
            if(ChunkOf(y)!=ChunkOf(y+1))
                AddEdge(x,y,x,y+1);
        }
    }

    private void AddEdge(int x1,int y1,int x2,int y2)
    {
        var a = RegionOf(x1,y1); var b = RegionOf(x2,y2);
        if(a.R==0||b.R==0||a.Equals(b)) return;
        if(!_edges.TryGetValue(a, out var s1)) _edges[a]=s1=new HashSet<RegionId>();
        if(!_edges.TryGetValue(b, out var s2)) _edges[b]=s2=new HashSet<RegionId>();
        s1.Add(b); s2.Add(a);
    }

    private RegionId RegionOf(int x,int y) => new(){Ci=(byte)(x/ChunkSize), Cj=(byte)(y/ChunkSize), R=_regionGrid[x,y]};
    private static int ChunkOf(int c) => c / ChunkSize;

    private void BuildGlobalRegions()
    {
        var visited = new HashSet<RegionId>();
        foreach(var kv in _edges)
        {
            if(visited.Contains(kv.Key)) continue;
            var q=new Queue<RegionId>(); q.Enqueue(kv.Key); visited.Add(kv.Key);
            int gid=_nextGlobalId++;
            while(q.Count>0)
            {
                var r=q.Dequeue(); _globalRegion[r]=gid;
                if(!_edges.TryGetValue(r,out var neigh)) continue;
                foreach(var n in neigh) if(visited.Add(n)) q.Enqueue(n);
            }
        }
    }

    public bool IsReachable(int x0,int y0,int x1,int y1, Grid<byte> pass)
    {
        if(pass.Get(x0,y0)==0 || pass.Get(x1,y1)==0) return false;
        var a=RegionOf(x0,y0); var b=RegionOf(x1,y1);
        if(a.R==0||b.R==0) return false;
        if(!_globalRegion.TryGetValue(a,out int ga) || !_globalRegion.TryGetValue(b,out int gb)) return a.Equals(b);
        return ga==gb;
    }

    public bool MakeGoalReachable(int x0,int y0, ref int gx,int gy, Grid<byte> pass)
    {
        if(pass.Get(gx,gy)!=0 && IsReachable(x0,y0,gx,gy,pass)) return true;
        // Spiral search for nearest reachable goal cell (mirrors 0 A.D. MakeGoalReachable)
        int bestDist=int.MaxValue, bx=gx, by=gy;
        for(int r=0;r<16;r++) for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++)
        {
            int nx=gx+dx, ny=gy+dy;
            if(nx<0||ny<0||nx>=_w||ny>=_h) continue;
            if(pass.Get(nx,ny)==0) continue;
            if(!IsReachable(x0,y0,nx,ny,pass)) continue;
            int d=dx*dx+dy*dy;
            if(d<bestDist){bestDist=d; bx=nx; by=ny;}
        }
        if(bestDist==int.MaxValue) return false;
        gx=bx; gy=by; return true;
    }
}

/// <summary>
/// Long-range A* pathfinder — condensed port of LongPathfinder.cpp (1170 lines).
/// Uses Grid<byte> passability and HierarchicalPathfinder for early-out.
/// Deterministic: tie-breaking by insertion order, integer costs (10/14).
/// Runs on worker threads; caller must not mutate Grid during Compute.
/// </summary>
public sealed class LongPathfinder
{
    private readonly Grid<byte> _grid;
    private readonly HierarchicalPathfinder _hier;

    public LongPathfinder(Grid<byte> grid, HierarchicalPathfinder hier) { _grid = grid; _hier = hier; }

    public List<(int x,int y)> FindPath(int sx,int sy,int gx,int gy)
    {
        if(!_hier.MakeGoalReachable(sx,sy, ref gx, ref gy, _grid)) return null;
        if(sx==gx && sy==gy) return new List<(int,int)>{(sx,sy)};

        int w=_grid.Width, h=_grid.Height;
        var open = new PriorityQueue<(int x,int y,int g,int h,int px,int py,int tie), int>();
        var gScore = new Dictionary<long,int>();
        var cameFrom = new Dictionary<long,(int x,int y)>();
        int tie=0;
        long Key(int x,int y)=>((long)x<<32)|(uint)y;
        int Heuristic(int x,int y){ int dx=Math.Abs(x-gx), dy=Math.Abs(y-gy); return 10*(dx+dy)+(14-20)*Math.Min(dx,dy); }
        gScore[Key(sx,sy)]=0;
        open.Enqueue((sx,sy,0,Heuristic(sx,sy),-1,-1,tie++), Heuristic(sx,sy)*1000+tie);

        int[] dx8={1,1,0,-1,-1,-1,0,1}, dy8={0,1,1,1,0,-1,-1,-1}, cost8={10,14,10,14,10,14,10,14};
        var closed=new HashSet<long>();

        while(open.Count>0)
        {
            var cur=open.Dequeue();
            long ck=Key(cur.x,cur.y);
            if(closed.Contains(ck)) continue;
            closed.Add(ck);
            if(cur.x==gx && cur.y==gy)
            {
                var path=new List<(int,int)>(); int cx=gx, cy=gy;
                while(true){ path.Add((cx,cy)); if(cx==sx&&cy==sy) break; var p=cameFrom[Key(cx,cy)]; cx=p.x; cy=p.y; }
                path.Reverse(); return path;
            }
            for(int k=0;k<8;k++)
            {
                int nx=cur.x+dx8[k], ny=cur.y+dy8[k];
                if(nx<0||ny<0||nx>=w||ny>=h) continue;
                if(_grid.Get(nx,ny)==0) continue;
                // No corner-cutting through blocked diagonals
                if(k%2==1 && (_grid.Get(cur.x+dx8[k],cur.y)==0 || _grid.Get(cur.x,cur.y+dy8[k])==0)) continue;
                int ng=cur.g+cost8[k];
                long nk=Key(nx,ny);
                if(gScore.TryGetValue(nk,out int og) && ng>=og) continue;
                gScore[nk]=ng; cameFrom[nk]=(cur.x,cur.y);
                int nh=Heuristic(nx,ny);
                open.Enqueue((nx,ny,ng,nh,cur.x,cur.y,tie), (ng+nh)*1000+tie); tie++;
            }
        }
        return null;
    }
}
