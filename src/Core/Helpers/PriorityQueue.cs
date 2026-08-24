/* Ported from source/simulation2/helpers/PriorityQueue.h — 0 A.D. GPL-2.0 */
/// Ported from source/simulation2/helpers/PriorityQueue.h — 0 A.D. GPL-2.0
/// Deterministic binary heap, same API as 0 A.D. PriorityQueueHeap/List.
/// Tie-breaks on rank, then h, then id for determinism.
/// Also provides generic PriorityQueue<TElement,TPriority> for LongPathfinder.

using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Priority item — mirrors PriorityQueueHeap::Item (id, rank, h).</summary>
public struct PriorityQueueItem<TId, TRank, TH>
    where TId : IComparable<TId>
    where TRank : IComparable<TRank>
    where TH : IComparable<TH>
{
    public TId Id;
    public TRank Rank;
    public TH H;
}

internal sealed class QueueItemComparer<TId, TRank, TH> : IComparer<PriorityQueueItem<TId, TRank, TH>>
    where TId : IComparable<TId>
    where TRank : IComparable<TRank>
    where TH : IComparable<TH>
{
    public int Compare(PriorityQueueItem<TId, TRank, TH> a, PriorityQueueItem<TId, TRank, TH> b)
    {
        int c = a.Rank.CompareTo(b.Rank);
        if (c != 0) return c;
        c = a.H.CompareTo(b.H);
        if (c != 0) return c;
        return a.Id.CompareTo(b.Id);
    }
}

/// <summary>Binary-heap priority queue. Port of PriorityQueueHeap. Deterministic.</summary>
public sealed class PriorityQueueHeap<TId, TRank, TH>
    where TId : IComparable<TId>
    where TRank : IComparable<TRank>
    where TH : IComparable<TH>
{
    private readonly List<PriorityQueueItem<TId, TRank, TH>> _heap = new();
    private readonly IComparer<PriorityQueueItem<TId, TRank, TH>> _cmp = new QueueItemComparer<TId, TRank, TH>();

    public int Count => _heap.Count;
    public bool Empty() => _heap.Count == 0;
    public void Clear() => _heap.Clear();

    public void Push(PriorityQueueItem<TId, TRank, TH> item)
    {
        _heap.Add(item);
        SiftUp(_heap.Count - 1);
    }
    // Alias for 0 A.D. push naming
    public void push(PriorityQueueItem<TId, TRank, TH> item) => Push(item);

    public PriorityQueueItem<TId, TRank, TH> Pop()
    {
        var top = _heap[0];
        int last = _heap.Count - 1;
        _heap[0] = _heap[last];
        _heap.RemoveAt(last);
        if (_heap.Count > 0) SiftDown(0);
        return top;
    }
    public PriorityQueueItem<TId, TRank, TH> pop() => Pop();
    public bool empty() => Empty();
    public int size() => Count;
    public void clear() => Clear();

    public void Promote(TId id, TRank oldRank, TRank newRank, TH newH)
    {
        for (int i = _heap.Count - 1; i >= 0; i--)
        {
            if (_heap[i].Id.CompareTo(id) != 0) continue;
            var it = _heap[i];
            it.Rank = newRank; it.H = newH;
            _heap[i] = it;
            SiftUp(i);
            return;
        }
    }
    public void promote(TId id, TRank oldRank, TRank newRank, TH newH) => Promote(id, oldRank, newRank, newH);

    private void SiftUp(int i)
    {
        while (i > 0)
        {
            int p = (i - 1) >> 1;
            if (_cmp.Compare(_heap[i], _heap[p]) >= 0) break;
            (_heap[i], _heap[p]) = (_heap[p], _heap[i]);
            i = p;
        }
    }
    private void SiftDown(int i)
    {
        int n = _heap.Count;
        while (true)
        {
            int l = (i << 1) + 1, r = l + 1, smallest = i;
            if (l < n && _cmp.Compare(_heap[l], _heap[smallest]) < 0) smallest = l;
            if (r < n && _cmp.Compare(_heap[r], _heap[smallest]) < 0) smallest = r;
            if (smallest == i) break;
            (_heap[i], _heap[smallest]) = (_heap[smallest], _heap[i]);
            i = smallest;
        }
    }
}

/// <summary>Array-scan queue — port of PriorityQueueList. Pop O(n), push O(1).</summary>
public sealed class PriorityQueueList<TId, TRank, TH>
    where TId : IComparable<TId>
    where TRank : IComparable<TRank>
    where TH : IComparable<TH>
{
    private readonly List<PriorityQueueItem<TId, TRank, TH>> _list = new();
    private readonly IComparer<PriorityQueueItem<TId, TRank, TH>> _cmp = new QueueItemComparer<TId, TRank, TH>();

    public bool Empty() => _list.Count == 0;
    public int Count => _list.Count;
    public void Clear() => _list.Clear();
    public void Push(PriorityQueueItem<TId, TRank, TH> item) => _list.Add(item);
    public void push(PriorityQueueItem<TId, TRank, TH> item) => Push(item);
    public bool empty() => Empty();
    public void clear() => Clear();

    public PriorityQueueItem<TId, TRank, TH> Pop()
    {
        int best = _list.Count - 1;
        for (int i = best - 1; i >= 0; i--)
            if (_cmp.Compare(_list[i], _list[best]) < 0) best = i;
        var ret = _list[best];
        _list[best] = _list[_list.Count - 1];
        _list.RemoveAt(_list.Count - 1);
        return ret;
    }
    public PriorityQueueItem<TId, TRank, TH> pop() => Pop();

    public PriorityQueueItem<TId, TRank, TH>? Find(TId id)
    {
        foreach (var it in _list) if (it.Id.CompareTo(id) == 0) return it;
        return null;
    }
    public void Promote(TId id, TRank oldRank, TRank newRank, TH newH)
    {
        for (int i = 0; i < _list.Count; i++)
            if (_list[i].Id.CompareTo(id) == 0) { var it = _list[i]; it.Rank = newRank; it.H = newH; _list[i] = it; return; }
    }
    public void promote(TId id, TRank oldRank, TRank newRank, TH newH) => Promote(id, oldRank, newRank, newH);
}

/// <summary>Simple heap for LongPathfinder (element + priority). Deterministic min-heap.</summary>
public sealed class PriorityQueue<TElement, TPriority> where TPriority : IComparable<TPriority>
{
    private readonly List<(TElement element, TPriority priority, long tie)> _heap = new();
    private long _counter;
    public int Count => _heap.Count;
    public void Clear() => _heap.Clear();
    public void Enqueue(TElement element, TPriority priority) { _heap.Add((element, priority, _counter++)); SiftUp(_heap.Count - 1); }
    public TElement Dequeue() { var top = _heap[0].element; int last = _heap.Count - 1; _heap[0] = _heap[last]; _heap.RemoveAt(last); if (_heap.Count > 0) SiftDown(0); return top; }
    public TElement Peek() => _heap[0].element;
    public bool TryDequeue(out TElement e, out TPriority p) { if (_heap.Count == 0) { e = default!; p = default!; return false; } var t = _heap[0]; e = t.element; p = t.priority; Dequeue(); return true; }
    private void SiftUp(int i) { while (i > 0) { int p = (i - 1) >> 1; if (Cmp(i, p) >= 0) break; Swap(i, p); i = p; } }
    private void SiftDown(int i) { int n = _heap.Count; while (true) { int l = (i << 1) + 1, r = l + 1, s = i; if (l < n && Cmp(l, s) < 0) s = l; if (r < n && Cmp(r, s) < 0) s = r; if (s == i) break; Swap(i, s); i = s; } }
    private int Cmp(int a, int b) { int c = _heap[a].priority.CompareTo(_heap[b].priority); return c != 0 ? c : _heap[a].tie.CompareTo(_heap[b].tie); }
    private void Swap(int a, int b) => (_heap[a], _heap[b]) = (_heap[b], _heap[a]);
}
