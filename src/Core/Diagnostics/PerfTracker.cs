/* Perf infra — reference: 0 A.D. source/tools/profiler2, source/ps/Profiler2/{Profiler2.cpp,h} + source/tools/atlas/GameInterface */
/// <remarks>
/// 0 A.D. Profiler2 samples per-frame GPU/CPU via timer queries (Renderer::BeginFrame → EndFrame).
/// Ported pattern: BeginFrame() snapshots Engine FPS + Time tick, RecordTick() wraps GameSim.Tick,
/// RecordDraw() tallies WorldView MultiMesh batches; GPU ms via RenderingDevice / RenderingServer if Vulkan.
/// Ring History (120 samples ≈ 2s @60fps) feeds DebugOverlay graph. Headless-safe: all rendering calls guarded.
/// </remarks>
using System;
using System.Collections.Generic;
using System.Text;
using Godot;

namespace Rts0.Core;

/// <summary>Single frame sample for graph/history.</summary>
public readonly struct PerfSample
{
    public readonly double Timestamp;
    public readonly float Fps;
    public readonly float FrameMs;
    public readonly float TickMs;
    public readonly float GpuMs;
    public readonly float RttMs;
    public readonly int Units;
    public readonly int Buildings;
    public readonly int DrawCalls;
    public PerfSample(double ts, float fps, float frameMs, float tickMs, float gpuMs, float rttMs, int units, int buildings, int draws)
    { Timestamp = ts; Fps = fps; FrameMs = frameMs; TickMs = tickMs; GpuMs = gpuMs; RttMs = rttMs; Units = units; Buildings = buildings; DrawCalls = draws; }
}

public static class PerfTracker
{
    private const int HistoryCap = 120;
    private static readonly Queue<PerfSample> _history = new();
    private static readonly object _sync = new();
    private static double _frameStartMsec;
    private static float _lastTickMs;
    private static int _lastUnits, _lastBuildings, _lastDraws;
    private static float _lastGpuMs, _lastRttMs = -1f; // -1 = placeholder (no network yet)
    private static float _lastFps, _lastFrameMs;

    public static IReadOnlyCollection<PerfSample> History { get { lock (_sync) return _history.ToArray(); } }

    /// <summary>Call at top of Main._Process / _PhysicsProcess before tick.</summary>
    public static void BeginFrame()
    {
        try
        {
            _frameStartMsec = Time.GetTicksMsec();
            _lastFps = (float)Engine.GetFramesPerSecond();
            _lastGpuMs = QueryGpuMs();
        }
        catch { _lastGpuMs = 0f; }
    }

    public static void RecordTick(double ms) { _lastTickMs = (float)ms; }

    /// <summary>Called from WorldView.Sync after culling/batching. drawCalls = MultiMesh batches actually drawn.</summary>
    public static void RecordDraw(int units, int buildings, int drawCalls = -1)
    {
        _lastUnits = units;
        _lastBuildings = buildings;
        _lastDraws = drawCalls < 0 ? EstimateDrawCalls(units, buildings) : drawCalls;
        float frameMs = 0f;
        try { frameMs = (float)(Time.GetTicksMsec() - _frameStartMsec); } catch { }
        _lastFrameMs = frameMs;
        var s = new PerfSample(Time.GetTicksMsec() / 1000.0, _lastFps, frameMs, _lastTickMs, _lastGpuMs, _lastRttMs, units, buildings, _lastDraws);
        lock (_sync)
        {
            if (_history.Count >= HistoryCap) _history.Dequeue();
            _history.Enqueue(s);
        }
    }

    public static void RecordRtt(float ms) { _lastRttMs = ms; } // placeholder for future netcode

    public static string GetReport()
    {
        lock (_sync)
        {
            var sb = new StringBuilder(256);
            sb.AppendLine($"FPS:{_lastFps:0}  frame:{_lastFrameMs:0.0}ms  tick:{_lastTickMs:0.00}ms  gpu:{_lastGpuMs:0.00}ms  rtt:{(_lastRttMs < 0 ? "—" : _lastRttMs.ToString("0.0") + "ms")}");
            sb.AppendLine($"units:{_lastUnits}  buildings:{_lastBuildings}  draws:{_lastDraws}  hist:{_history.Count}/{HistoryCap}");
            if (_history.Count > 1)
            {
                float avgTick = 0, maxTick = 0; foreach (var h in _history) { avgTick += h.TickMs; if (h.TickMs > maxTick) maxTick = h.TickMs; }
                avgTick /= _history.Count;
                sb.AppendLine($"tick avg:{avgTick:0.00}ms  max:{maxTick:0.00}ms");
            }
            return sb.ToString();
        }
    }

    private static int EstimateDrawCalls(int units, int buildings) =>
        (units > 0 ? 2 : 0) + (buildings > 0 ? 2 : 0) + 1 + 2; // player+enemy + center+house + terrain + tree+gold ≈ 7 max

    private static float QueryGpuMs()
    {
        try
        {
            var rd = RenderingServer.GetRenderingDevice();
            if (rd != null) return 0f; // RenderingDevice timer queries require explicit query objects; placeholder until wired to compute/graph pipeline
            // Fallback: measured render time if available (Godot 4.3 RenderingServer.VIEWPORT_INFO_TYPE_VISIBLE)
            // RenderingServer.GetRenderingInfo is not GPU ms — return 0 as placeholder
            return 0f;
        }
        catch { return 0f; }
    }
}
