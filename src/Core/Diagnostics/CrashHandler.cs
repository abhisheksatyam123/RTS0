/* Ported from 0 A.D. source/ps/CrashHandler + source/ps/Log.cpp — GPL-2.0, simplified for Godot */
/// <summary>CrashHandler — captures unhandled exceptions → Logger.Fatal + crash dump.</summary>
/// <remarks>
/// Reference: 0ad source/ps/Log.cpp (crash file) + source/lib/debug.cpp (stack dump).
/// Simplified for Godot: AppDomain.UnhandledException + TaskScheduler.UnobservedTaskException → user://logs/crash_{timestamp}.log
/// Hook Func&lt;string&gt; allows GameSim snapshot injection without coupling.
/// </remarks>
using System;
using System.Threading.Tasks;
using Godot;

namespace Rts0.Core;

public static class CrashHandler
{
    private static bool _installed;
    private static readonly object _sync = new();
    private static Func<string> _snapshotHook;
    private const string LogDir = "user://logs";

    /// <summary>Install global handlers. Call once at startup (Main._Ready). Thread-safe.</summary>
    public static void Install(Func<string> simSnapshot = null)
    {
        lock (_sync)
        {
            if (_installed) return;
            _snapshotHook = simSnapshot;
            AppDomain.CurrentDomain.UnhandledException += OnUnhandled;
            TaskScheduler.UnobservedTaskException += OnUnobserved;
            _installed = true;
            Logger.Info("CrashHandler", "installed");
        }
    }

    public static void SetSnapshotHook(Func<string> hook)
    {
        lock (_sync) _snapshotHook = hook;
    }

    public static void Uninstall()
    {
        lock (_sync)
        {
            if (!_installed) return;
            AppDomain.CurrentDomain.UnhandledException -= OnUnhandled;
            TaskScheduler.UnobservedTaskException -= OnUnobserved;
            _installed = false;
        }
    }

    private static void OnUnhandled(object _, UnhandledExceptionEventArgs e)
    {
        var ex = e.ExceptionObject as Exception ?? new Exception(e.ExceptionObject?.ToString() ?? "unknown");
        Handle(ex, "AppDomain.UnhandledException", e.IsTerminating);
    }

    private static void OnUnobserved(object _, UnobservedTaskExceptionEventArgs e)
    {
        Handle(e.Exception, "TaskScheduler.UnobservedTaskException", false);
        e.SetObserved();
    }

    private static void Handle(Exception ex, string context, bool terminating)
    {
        try { Logger.Fatal("Crash", $"{context} terminating={terminating}: {ex}"); } catch { }
        try { WriteCrashDump(ex, context, terminating); } catch { }
        // Ensure log file flushed before process exit
        GD.PushError($"[CrashHandler] {context}: {ex.Message}");
    }

    private static void WriteCrashDump(Exception ex, string context, bool terminating)
    {
        try { DirAccess.MakeDirRecursiveAbsolute(LogDir); } catch { }
        try
        {
            var d = DirAccess.Open("user://");
            d?.MakeDirRecursive("logs");
        }
        catch { }

        string ts = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        string path = $"user://logs/crash_{ts}.log";
        string snapshot = "";
        lock (_sync)
        {
            try { snapshot = _snapshotHook?.Invoke() ?? "(no snapshot hook)"; } catch (Exception se) { snapshot = $"snapshot hook failed: {se}"; }
        }

        string content = $"RTS0 crash dump {ts}\n"
            + $"context: {context}\n"
            + $"terminating: {terminating}\n"
            + $"os: {OS.GetName()} godot={Engine.GetVersionInfo()["string"]}\n"
            + $"exception: {ex.GetType().FullName}: {ex.Message}\n"
            + $"stack:\n{ex.StackTrace}\n"
            + $"inner: {ex.InnerException}\n"
            + $"--- sim snapshot ---\n{snapshot}\n"
            + $"--- recent logs ---\n{FormatRecent()}\n";

        FileAccess f = null;
        try
        {
            f = FileAccess.Open(path, FileAccess.ModeFlags.Write);
            f?.StoreString(content);
            f?.Flush();
            f?.Close();
            Logger.Info("CrashHandler", $"crash dump written to {path}");
        }
        catch (Exception we)
        {
            try { Logger.Error("CrashHandler", $"failed to write crash dump: {we}"); } catch { }
        }
        finally { try { f?.Close(); } catch { } }
    }

    private static string FormatRecent()
    {
        try
        {
            var recent = Logger.GetRecent(64);
            if (recent.Count == 0) return "(no recent logs)";
            var s = "";
            foreach (var e in recent) s += e.Formatted + "\n";
            return s;
        }
        catch { return "(failed to get recent logs)"; }
    }
}
