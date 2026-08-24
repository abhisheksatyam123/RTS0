/* BugReporter — reference: 0 A.D. source/ps/Log.cpp (crash file) + source/tools/atlas + source/lib/debug.cpp (system info dump) */
/// <remarks>
/// Collects device info (OS, Godot version/rendering method, Vulkan adapter, locale), sim snapshot
/// (Gold/Wave/units/buildings if hook registered), Logger tail, PerfTracker report and writes to
/// user://logs/bug_{timestamp}.log for `adb pull` / desktop open. Mirrors 0 A.D. bug report bundle.
/// Thread-safe; all Godot calls guarded for headless.
/// </remarks>
using System;
using System.Text;
using Godot;

namespace Rts0.Core;

public static class BugReporter
{
    private const string LogDir = "user://logs";
    private static Func<string> _simHook;
    private static readonly object _sync = new();

    public static void SetSnapshotHook(Func<string> hook) { lock (_sync) _simHook = hook; }
    public static void SetSnapshotHook(GameSim sim) { lock (_sync) _simHook = sim != null ? () => Snapshot(sim) : null; }

    /// <summary>Capture bug report → absolute user:// path. Title sanitized for filename.</summary>
    public static string Capture(string title) => Capture(title, null);

    public static string Capture(string title, GameSim sim)
    {
        if (sim != null) SetSnapshotHook(sim);
        string safe = Sanitize(title);
        string ts = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        string fname = $"bug_{ts}_{safe}.log";
        string path = $"{LogDir}/{fname}";

        string content = BuildReport(title, ts, sim);

        try { DirAccess.MakeDirRecursiveAbsolute(LogDir); } catch { }
        try { var d = DirAccess.Open("user://"); d?.MakeDirRecursive("logs"); } catch { }

        FileAccess f = null;
        try
        {
            f = FileAccess.Open(path, FileAccess.ModeFlags.Write);
            if (f == null) throw new Exception($"FileAccess.Open returned null for {path}: {FileAccess.GetOpenError()}");
            f.StoreString(content);
            f.Flush(); f.Close();
            try { Logger.Info("BugReporter", $"bug report written to {path}"); } catch { }
            return path;
        }
        catch (Exception ex)
        {
            try { f?.Close(); } catch { }
            try { Logger.Error("BugReporter", $"failed to write {path}: {ex}"); } catch { }
            GD.PushError($"[BugReporter] {ex}");
            return path; // still return intended path for caller
        }
    }

    private static string BuildReport(string title, string ts, GameSim sim)
    {
        var sb = new StringBuilder(2048);
        sb.AppendLine($"RTS0 bug report — {ts}");
        sb.AppendLine($"title: {title}");
        sb.AppendLine($"--- device ---");
        sb.AppendLine(DeviceInfo());
        sb.AppendLine($"--- sim snapshot ---");
        sb.AppendLine(SimSnapshot(sim));
        sb.AppendLine($"--- perf ---");
        try { sb.AppendLine(PerfTracker.GetReport()); } catch (Exception ex) { sb.AppendLine($"(perf failed: {ex.Message})"); }
        sb.AppendLine($"--- recent logs (64) ---");
        try
        {
            var recent = Logger.GetRecent(64);
            if (recent.Count == 0) sb.AppendLine("(no logs)");
            else foreach (var e in recent) sb.AppendLine(e.Formatted);
        }
        catch (Exception ex) { sb.AppendLine($"(logs failed: {ex.Message})"); }
        return sb.ToString();
    }

    private static string DeviceInfo()
    {
        var sb = new StringBuilder();
        try { sb.AppendLine($"os: {OS.GetName()} {OS.GetVersion()} distribution={OS.GetDistributionName()}"); } catch { sb.AppendLine("os: (unknown)"); }
        try { var vi = Engine.GetVersionInfo(); sb.AppendLine($"godot: {vi["string"]} ({vi["major"]}.{vi["minor"]}.{vi["patch"]}-{vi["status"]})"); } catch { sb.AppendLine("godot: (unknown)"); }
        try { sb.AppendLine($"rendering: {RenderingServer.GetCurrentRenderingMethod()} adapter={RenderingServer.GetVideoAdapterName()} vendor={RenderingServer.GetVideoAdapterVendor()}"); } catch { sb.AppendLine("rendering: (unknown)"); }
        try { var rd = RenderingServer.GetRenderingDevice(); sb.AppendLine($"vulkan: {(rd != null ? "yes" : "no (GL/Headless)") }"); } catch { sb.AppendLine("vulkan: (unknown)"); }
        try { sb.AppendLine($"locale: {TranslationServer.GetLocale()} os_locale={OS.GetLocaleLanguage()}_{OS.GetLocaleCountry()}"); } catch { sb.AppendLine("locale: (unknown)"); }
        try { sb.AppendLine($"user_data: {OS.GetUserDataDir()} processor={OS.GetProcessorName()} cores={OS.GetProcessorCount()}"); } catch { }
        return sb.ToString();
    }

    private static string SimSnapshot(GameSim sim)
    {
        // Prefer explicit sim arg, else hook (Main can register live sim)
        if (sim == null) lock (_sync) { try { var h = _simHook?.Invoke(); if (!string.IsNullOrEmpty(h)) return h; } catch (Exception ex) { return $"(hook failed: {ex.Message})"; } }
        if (sim != null) return Snapshot(sim);
        return "(no sim — call BugReporter.SetSnapshotHook(sim) at startup or pass GameSim to Capture)";
    }

    private static string Snapshot(GameSim sim)
    {
        try { return $"Gold={sim.Gold} Wave={sim.WaveNumber} Units={sim.Units.Count} Buildings={sim.Buildings.Count} Resources={sim.Resources.Count}"; }
        catch (Exception ex) { return $"(snapshot failed: {ex.Message})"; }
    }

    private static string Sanitize(string s)
    {
        if (string.IsNullOrWhiteSpace(s)) return "untitled";
        var sb = new StringBuilder(s.Length);
        foreach (char c in s) sb.Append(char.IsLetterOrDigit(c) || c == '-' || c == '_' ? c : '_');
        string r = sb.ToString().Trim('_');
        return r.Length > 32 ? r[..32] : r;
    }
}
