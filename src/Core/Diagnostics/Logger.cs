/* Ported from 0 A.D. source/ps/Log.cpp — GPL-2.0, simplified for Godot 4.3 C# */
/// <summary>Logger — Godot port of 0 A.D. source/ps/Log.cpp style but simplified.</summary>
/// <remarks>
/// Reference: 0ad source/ps/Log.cpp (log levels, tagged output, file rotation, sink abstraction).
/// Simplified for Godot: GD.Print → logcat on Android, FileAccess → user://logs/rts0.log, ring → overlay.
/// Thread-safe via lock; file via Godot FileAccess (user:// works on Android + desktop).
/// </remarks>
using System;
using System.Collections.Generic;
using Godot;

namespace Rts0.Core;

/// <summary>Log severity. Mirrors 0 A.D. LogLevel — higher = more severe.</summary>
public enum LogLevel { Verbose = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5 }

/// <summary>Pluggable sink. Mirrors 0 A.D. log output abstraction.</summary>
public interface ILogSink
{
    void Write(LogLevel level, string tag, string message, string formatted);
}

/// <summary>Single ring entry for in-game overlay.</summary>
public readonly struct LogEntry
{
    public readonly double Time;
    public readonly LogLevel Level;
    public readonly string Tag;
    public readonly string Message;
    public readonly string Formatted;
    public LogEntry(double t, LogLevel lvl, string tag, string msg, string fmt)
    { Time = t; Level = lvl; Tag = tag; Message = msg; Formatted = fmt; }
}

/// <summary>
/// Static logger: GD.Print (→ adb logcat on Android) + file + ring.
/// Thread-safe. File: user://logs/rts0.log with rotation via FileAccess + DirAccess.
/// API: Logger.Info("tag","msg"), Logger.Warn, Logger.Error(ex), SetLevel, AddSink.
/// </summary>
public static class Logger
{
    private static LogLevel _level = LogLevel.Info;
    private static readonly object _sync = new();
    private static readonly List<ILogSink> _sinks = new();
    private static readonly Queue<LogEntry> _ring = new();
    private const int RingCapacity = 256;
    private const string LogDir = "user://logs";
    private const string LogPath = "user://logs/rts0.log";
    private const string LogPath1 = "user://logs/rts0.log.1";
    private const long MaxBytes = 512 * 1024; // 512 KB rotation
    private static FileAccess _file;

    static Logger()
    {
        try { EnsureFile(); } catch { }
        AppDomain.CurrentDomain.ProcessExit += (_, _) => CloseFile();
    }

    public static void Init(string name)
    {
        Info("Logger", $"Init {name} — level={GetLevel()}");
        try { DirAccess.MakeDirRecursiveAbsolute(LogDir); } catch { }
    }

    public static LogLevel GetLevel() { lock (_sync) return _level; }
    public static void SetLevel(LogLevel lvl) { lock (_sync) _level = lvl; }
    public static void AddSink(ILogSink sink) { lock (_sync) if (sink != null && !_sinks.Contains(sink)) _sinks.Add(sink); }
    public static void RemoveSink(ILogSink sink) { lock (_sync) _sinks.Remove(sink); }

    // --- convenience API ---
    public static void Verbose(string tag, string msg) => Log(LogLevel.Verbose, tag, msg);
    public static void Debug(string tag, string msg) => Log(LogLevel.Debug, tag, msg);
    public static void Info(string tag, string msg) => Log(LogLevel.Info, tag, msg);
    public static void Warn(string tag, string msg) => Log(LogLevel.Warn, tag, msg);
    public static void Error(string tag, string msg) => Log(LogLevel.Error, tag, msg);
    public static void Error(string tag, Exception ex) => Log(LogLevel.Error, tag, ex?.ToString() ?? "null exception");
    public static void Error(Exception ex) => Log(LogLevel.Error, "Exception", ex?.ToString() ?? "null exception");
    public static void Fatal(string tag, string msg) => Log(LogLevel.Fatal, tag, msg);
    public static void Fatal(string tag, Exception ex) => Log(LogLevel.Fatal, tag, ex?.ToString() ?? "null exception");

    public static void Log(LogLevel level, string tag, string message)
    {
        if (level < GetLevel()) return;
        string ts = DateTime.Now.ToString("HH:mm:ss.fff");
        string tid = System.Threading.Thread.CurrentThread.ManagedThreadId.ToString();
        string formatted = $"[{ts}] [{level.ToString().ToUpper()}] [{tag}:{tid}] {message}";
        LogEntry entry = new(Time.GetTicksMsec() / 1000.0, level, tag, message, formatted);

        List<ILogSink> sinksCopy;
        lock (_sync)
        {
            if (_ring.Count >= RingCapacity) _ring.Dequeue();
            _ring.Enqueue(entry);
            sinksCopy = new List<ILogSink>(_sinks);
            try { RotateIfNeeded(); } catch { }
            try
            {
                _file?.StoreString(formatted + "\n");
                _file?.Flush();
            }
            catch { }
        }

        // GD.Print → logcat on Android (godot:V), desktop stdout
        if (level >= LogLevel.Error) GD.PushError(formatted);
        else GD.Print(formatted);

        foreach (var s in sinksCopy) try { s.Write(level, tag, message, formatted); } catch { }
    }

    public static IReadOnlyList<LogEntry> GetRecent(int count = RingCapacity)
    {
        lock (_sync)
        {
            var arr = _ring.ToArray();
            if (count >= arr.Length) return arr;
            var slice = new LogEntry[count];
            Array.Copy(arr, arr.Length - count, slice, 0, count);
            return slice;
        }
    }

    public static void ClearRing() { lock (_sync) _ring.Clear(); }

    private static void EnsureFile()
    {
        try { DirAccess.MakeDirRecursiveAbsolute(LogDir); } catch { }
        // Also try via DirAccess.Open fallback
        try
        {
            var d = DirAccess.Open("user://");
            if (d != null) d.MakeDirRecursive("logs");
        }
        catch { }

        if (_file != null) return;
        try
        {
            if (FileAccess.FileExists(LogPath))
                _file = FileAccess.Open(LogPath, FileAccess.ModeFlags.ReadWrite);
            else
                _file = FileAccess.Open(LogPath, FileAccess.ModeFlags.Write);
            _file?.SeekEnd();
        }
        catch { _file = null; }
    }

    private static void RotateIfNeeded()
    {
        if (_file == null) { EnsureFile(); if (_file == null) return; }
        try
        {
            if (_file.GetLength() <= (ulong)MaxBytes) return;
            _file.Close(); _file = null;
            try
            {
                if (FileAccess.FileExists(LogPath1))
                {
                    var da = DirAccess.Open(LogDir);
                    da?.Remove("rts0.log.1");
                }
            }
            catch { }
            try
            {
                var da = DirAccess.Open(LogDir);
                da?.Rename("rts0.log", "rts0.log.1");
            }
            catch { }
            EnsureFile();
        }
        catch { }
    }

    private static void CloseFile()
    {
        lock (_sync)
        {
            try { _file?.Flush(); _file?.Close(); } catch { }
            _file = null;
        }
    }
}
