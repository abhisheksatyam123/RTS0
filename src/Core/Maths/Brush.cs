/// Ported from source/maths/Brush.h/.cpp — 0 A.D. GPL-2.0 — convex brush CSG.
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Convex brush. Port of 0 A.D. <c>CBrush</c> (source/maths/Brush.h/.cpp). Supports Slice/Intersect.</summary>
public sealed class Brush
{
    private const int NoVertex = -1;
    private readonly List<Vector3D> _verts = new();
    private readonly List<int> _faces = new();

    public Brush() { }
    public Brush(BoundingBoxAligned b)
    {
        _verts.Capacity = 8;
        for (int i = 0; i < 8; i++)
            _verts.Add(new Vector3D((i & 1) != 0 ? b.Max.X : b.Min.X, (i & 2) != 0 ? b.Max.Y : b.Min.Y, (i & 4) != 0 ? b.Max.Z : b.Min.Z));
        int[] f = { 0,1,3,2,0, 4,5,7,6,4, 0,2,6,4,0, 1,3,7,5,1, 0,1,5,4,0, 2,3,7,6,2 };
        _faces.AddRange(f);
    }

    public bool IsEmpty => _verts.Count == 0;
    public IReadOnlyList<Vector3D> GetVertices() => _verts;
    public void GetFaces(List<List<int>> o)
    {
        int s = 0;
        while (s < _faces.Count)
        {
            var cur = new List<int> { _faces[s] };
            int j = s + 1;
            while (j < _faces.Count && _faces[j] != _faces[s]) { cur.Add(_faces[j]); j++; }
            cur.Add(_faces[j]); o.Add(cur); s = j + 1;
        }
    }

    public void Bounds(out BoundingBoxAligned r)
    {
        r = BoundingBoxAligned.Empty;
        if (IsEmpty) return;
        var mn = _verts[0]; var mx = _verts[0];
        foreach (var v in _verts)
        {
            if (v.X < mn.X) mn = new Vector3D(v.X, mn.Y, mn.Z);
            if (v.Y < mn.Y) mn = new Vector3D(mn.X, v.Y, mn.Z);
            if (v.Z < mn.Z) mn = new Vector3D(mn.X, mn.Y, v.Z);
            if (v.X > mx.X) mx = new Vector3D(v.X, mx.Y, mx.Z);
            if (v.Y > mx.Y) mx = new Vector3D(mx.X, v.Y, mx.Z);
            if (v.Z > mx.Z) mx = new Vector3D(mx.X, mx.Y, v.Z);
        }
        r = new BoundingBoxAligned(mn, mx);
    }

    /// <summary>Slice by plane keeping front side. See Brush.cpp Slice.</summary>
    public void Slice(Plane plane, Brush result)
    {
        if (ReferenceEquals(result, this)) throw new InvalidOperationException("result != this");
        result._verts.Clear(); result._faces.Clear();
        if (IsEmpty) return;
        int n = _verts.Count;
        var dist = new float[n]; var keep = new int[n];
        for (int i = 0; i < n; i++) { dist[i] = plane.DistanceToPlane(_verts[i]); keep[i] = dist[i] >= 0 ? result._verts.Count : NoVertex; if (keep[i] != NoVertex) result._verts.Add(_verts[i]); }
        var newVerts = new Dictionary<(int,int), int>();
        var neighbours = new Dictionary<int, List<int>>();
        int SliceEdge(int a, int b)
        {
            var key = a < b ? (a,b) : (b,a);
            if (newVerts.TryGetValue(key, out int idx)) return idx;
            float inv = 1f / (dist[a] - dist[b]);
            var p = _verts[b] * (dist[a] * inv) + _verts[a] * (-dist[b] * inv);
            int r = result._verts.Count; result._verts.Add(p);
            newVerts[key] = r; neighbours[r] = new List<int>(2);
            return r;
        }
        // walk faces (5 indices per face close loop)
        int faceStart = NoVertex, resFaceStart = NoVertex;
        var pending = new List<int>(); int pendingEdge = NoVertex;
        // Actually reuse simpler face walk: iterate back-to-back faces array.
        // To handle adjacency for cap, track per-face new verts
        var faceNewVerts = new List<int>();
        var allCap = new List<int>();
        for (int i = 0; i < _faces.Count; i++)
        {
            if (faceStart == NoVertex) { faceStart = _faces[i]; resFaceStart = result._faces.Count; faceNewVerts.Clear(); continue; }
            int prev = _faces[i - 1], cur = _faces[i];
            bool pKeep = keep[prev] != NoVertex, cKeep = keep[cur] != NoVertex;
            if (!pKeep && cKeep) { int nv = SliceEdge(prev, cur); result._faces.Add(nv); result._faces.Add(keep[cur]); faceNewVerts.Add(nv); }
            else if (pKeep && cKeep) result._faces.Add(keep[cur]);
            else if (pKeep && !cKeep) { int nv = SliceEdge(prev, cur); result._faces.Add(nv); faceNewVerts.Add(nv); }
            if (cur == faceStart)
            {
                if (result._faces.Count > resFaceStart) result._faces.Add(result._faces[resFaceStart]);
                for (int k = 0; k < faceNewVerts.Count - 1; k += 2) { /* pair */ }
                allCap.AddRange(faceNewVerts);
                faceStart = NoVertex;
            }
        }
        if (allCap.Count >= 3)
        {
            // create cap face; order by angle around centroid for winding (approx)
            var cent = Vector3D.Zero; foreach (var id in allCap) cent += result._verts[id];
            cent /= allCap.Count;
            var nrm = plane.Norm.Normalized();
            Vector3D u = Math.Abs(nrm.X) < 0.9f ? new Vector3D(1,0,0) : new Vector3D(0,1,0);
            u = (u - nrm * u.Dot(nrm)).Normalized();
            var v = nrm.Cross(u);
            allCap.Sort((a,b) => MathF.Atan2(result._verts[a].Dot(v) - cent.Dot(v), result._verts[a].Dot(u) - cent.Dot(u))
                                 .CompareTo(MathF.Atan2(result._verts[b].Dot(v) - cent.Dot(v), result._verts[b].Dot(u) - cent.Dot(u))));
            foreach (var id in allCap) result._faces.Add(id);
            result._faces.Add(allCap[0]);
        }
        if (result._verts.Count == 0) result._faces.Clear();
    }

    /// <summary>Intersect with frustum by repeated slicing. See Brush.cpp Intersect.</summary>
    public void Intersect(Frustum frustum, Brush result)
    {
        if (frustum.NumPlanes == 0) { result._verts.Clear(); result._verts.AddRange(_verts); result._faces.Clear(); result._faces.AddRange(_faces); return; }
        var buf = new Brush();
        Brush prev = this, next = (frustum.NumPlanes & 1) != 0 ? result : buf;
        for (int i = 0; i < frustum.NumPlanes; i++) { prev.Slice(frustum[i], next); prev = next; next = ReferenceEquals(prev, buf) ? result : buf; }
    }
}
