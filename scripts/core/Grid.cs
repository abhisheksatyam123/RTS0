/* Copyright (C) 2025 Wildfire Games.
 * Ported from source/simulation2/helpers/Grid.h (412 lines)
 * 0 A.D. - GPLv2. See LICENSE in ../0ad for details.
 * Direct C# port for RTS0 - deterministic tile storage.
 */

using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>
/// Deterministic 2D grid — direct port of 0 A.D. Grid<T>.
/// Backed by T[] row-major (y*W+x). T must be POD-equivalent (blittable).
/// Mirrors Grid.h API: width/height/reset/add/bitwise_or/any_set_in_square.
/// </summary>
public sealed class Grid<T> where T : struct, IEquatable<T>
{
    private ushort _w;
    private ushort _h;
    private T[] _data;

    public Grid() { _w = 0; _h = 0; _data = null; }
    public Grid(ushort w, ushort h) { Resize(w, h); }

    public ushort Width => _w;
    public ushort Height => _h;
    public bool IsBlank => _w == 0 && _h == 0;
    public T[] RawData => _data;

    public void Resize(ushort w, ushort h)
    {
        _w = w; _h = h;
        if (w == 0 && h == 0) { _data = null; return; }
        _data = new T[w * h];
        Reset();
    }

    public void Reset()
    {
        if (_data != null) Array.Clear(_data, 0, _data.Length);
    }

    public void Clear() => Resize(0, 0);

    public T Get(int x, int y) => _data[y * _w + x];
    public void Set(int x, int y, T value) => _data[y * _w + x] = value;

    public void Add(Grid<T> other)
    {
        // T is generic; use dynamic addition via IAdditionOperators in .NET 7+
        // Keep simple: require T is numeric via converter. For RTS0 we instantiate
        // Grid<byte>/Grid<ushort> only, so handle those explicitly for determinism.
        if (typeof(T) == typeof(byte))
        {
            var a = (byte[])(object)_data;
            var b = (byte[])(object)other._data;
            for (int i = 0; i < a.Length; i++) a[i] = (byte)(a[i] + b[i]);
        }
        else if (typeof(T) == typeof(ushort))
        {
            var a = (ushort[])(object)_data;
            var b = (ushort[])(object)other._data;
            for (int i = 0; i < a.Length; i++) a[i] = (ushort)(a[i] + b[i]);
        }
        else if (typeof(T) == typeof(int))
        {
            var a = (int[])(object)_data;
            var b = (int[])(object)other._data;
            for (int i = 0; i < a.Length; i++) a[i] += b[i];
        }
        else throw new NotSupportedException($"Grid.Add not supported for {typeof(T)}");
    }

    public void BitwiseOr(Grid<T> other)
    {
        if (typeof(T) == typeof(byte))
        {
            var a = (byte[])(object)_data;
            var b = (byte[])(object)other._data;
            for (int i = 0; i < a.Length; i++) a[i] |= b[i];
        }
        else throw new NotSupportedException($"BitwiseOr only for byte grids");
    }

    /// <summary>Any non-zero in rectangle [x0,x1) x [y0,y1).</summary>
    public bool AnySetInSquare(int x0, int y0, int x1, int y1)
    {
        for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (!Get(x, y).Equals(default(T))) return true;
        return false;
    }

    public bool CompareSizes<T2>(Grid<T2> other) where T2 : struct, IEquatable<T2>
        => other != null && _w == other.Width && _h == other.Height;
}

/// <summary>Port of 0 A.D. SparseGrid — bucketed lazy allocation for large sparse maps.</summary>
public sealed class SparseGrid<T> where T : struct
{
    private const int BucketBits = 4;
    private const int BucketSize = 1 << BucketBits;

    private readonly ushort _w;
    private readonly ushort _h;
    private readonly ushort _bw;
    private readonly ushort _bh;
    private readonly T[][] _buckets;

    public SparseGrid(ushort w, ushort h)
    {
        _w = w; _h = h;
        _bw = (ushort)((w + BucketSize - 1) >> BucketBits);
        _bh = (ushort)((h + BucketSize - 1) >> BucketBits);
        _buckets = new T[_bw * _bh][];
    }

    public void Reset()
    {
        for (int i = 0; i < _buckets.Length; i++) _buckets[i] = null;
    }

    public T Get(int x, int y)
    {
        int b = (y >> BucketBits) * _bw + (x >> BucketBits);
        var bucket = _buckets[b];
        if (bucket == null) return default;
        return bucket[(y % BucketSize) * BucketSize + (x % BucketSize)];
    }

    public void Set(int x, int y, T value)
    {
        int b = (y >> BucketBits) * _bw + (x >> BucketBits);
        var bucket = _buckets[b];
        if (bucket == null) bucket = _buckets[b] = new T[BucketSize * BucketSize];
        bucket[(y % BucketSize) * BucketSize + (x % BucketSize)] = value;
    }
}

/// <summary>Port of GridUpdateInformation — incremental obstruction dirtiness tracking (see CCmpObstructionManager).</summary>
public sealed class GridUpdateInformation
{
    public bool Dirty;
    public bool GloballyDirty;
    public Grid<byte> DirtinessGrid = new();

    public void MergeAndClear(GridUpdateInformation other)
    {
        bool wasDirty = Dirty;
        Dirty |= other.Dirty;
        GloballyDirty |= other.GloballyDirty;
        if (!wasDirty) (DirtinessGrid, other.DirtinessGrid) = (other.DirtinessGrid, DirtinessGrid);
        else if (Dirty && !GloballyDirty) DirtinessGrid.BitwiseOr(other.DirtinessGrid);
        other.Clean();
    }

    public void Clean()
    {
        Dirty = false;
        GloballyDirty = false;
        DirtinessGrid.Reset();
    }
}
