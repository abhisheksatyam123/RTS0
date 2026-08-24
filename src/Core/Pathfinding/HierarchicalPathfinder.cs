/* Ported from source/simulation2/helpers/HierarchicalPathfinder.h/.cpp — 0 A.D. GPL-2.0 */

using System;
using System.Collections.Generic;

namespace Rts0.Core;

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
        for(int y=0;y<_h;y++) for(int x=0;x<_w-1;x++)
        {
            if(pass.Get(x,y)==0 || pass.Get(x+1,y)==0) continue;
            if(ChunkOf(x)!=ChunkOf(x+1)) AddEdge(x,y,x+1,y);
        }
        for(int y=0;y<_h-1;y++) for(int x=0;x<_w;x++)
        {
            if(pass.Get(x,y)==0 || pass.Get(x,y+1)==0) continue;
            if(ChunkOf(y)!=ChunkOf(y+1)) AddEdge(x,y,x,y+1);
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

    public bool MakeGoalReachable(int x0,int y0, ref int gx, ref int gy, Grid<byte> pass)
    {
        if(pass.Get(gx,gy)!=0 && IsReachable(x0,y0,gx,gy,pass)) return true;
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
