/* Ported from source/simulation2/helpers/LongPathfinder.cpp — 0 A.D. GPL-2.0 */

using System;
using System.Collections.Generic;

namespace Rts0.Core;

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
