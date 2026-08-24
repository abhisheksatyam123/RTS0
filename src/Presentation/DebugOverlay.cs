/* DebugOverlay — CanvasLayer HUD over Main. Reference: 0 A.D. source/gui/scripting/JSInterface_Console.cpp (console toggle) + source/ps/Profiler2 display */
using System;
using System.Text;
using Godot;
using Rts0.Core;

namespace Rts0.Presentation;

/// <summary>
/// Godot CanvasLayer overlay: FPS/tick/units/GPU batches + Logger tail + mini graph.
/// Toggle with ` (KEY_QUOTELEFT / KEY_ASCIITILDE) or F3. Headless-safe: guards viewport/rendering calls.
/// Attach as child of Main or autoload; defaults to hidden until toggled.
/// </summary>
public partial class DebugOverlay : CanvasLayer
{
    private Label _label;
    private PerfGraph _graph;
    private bool _visible = false;

    public override void _Ready()
    {
        Layer = 128;
        Visible = false;
        try
        {
            var root = new Control { Name = "DebugRoot" };
            root.SetAnchorsPreset(Control.LayoutPreset.FullRect);
            root.MouseFilter = Control.MouseFilterEnum.Ignore;
            AddChild(root);

            _label = new Label { Name = "DebugLabel", Position = new Vector2(8, 8) };
            _label.AddThemeFontSizeOverride("font_size", 12);
            _label.AddThemeColorOverride("font_color", new Color(0.9f, 0.95f, 1f));
            _label.AddThemeColorOverride("font_shadow_color", new Color(0, 0, 0, 0.7f));
            _label.AddThemeConstantOverride("shadow_offset_x", 1);
            _label.AddThemeConstantOverride("shadow_offset_y", 1);
            root.AddChild(_label);

            _graph = new PerfGraph { Name = "PerfGraph", Position = new Vector2(8, 96), CustomMinimumSize = new Vector2(260, 48) };
            root.AddChild(_graph);
            _label.Text = "DebugOverlay ready — press ~ / F3";
        }
        catch (Exception ex) { try { Logger.Warn("DebugOverlay", $"init failed (headless?): {ex.Message}"); } catch { } }
    }

    public override void _Process(double delta)
    {
        if (!_visible || _label == null) return;
        try { _label.Text = BuildText(); _graph?.QueueRedraw(); }
        catch { }
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is not InputEventKey k || !k.Pressed || k.Echo) return;
        if (k.Keycode == Key.F3 || k.PhysicalKeycode == Key.F3 || k.Keycode == Key.Quoteleft || k.Keycode == Key.Asciitilde || k.PhysicalKeycode == Key.Quoteleft)
        {
            Toggle();
            GetViewport()?.SetInputAsHandled();
        }
    }

    public void Toggle() { _visible = !_visible; Visible = _visible; }

    private static string BuildText()
    {
        var sb = new StringBuilder(512);
        try { sb.Append(PerfTracker.GetReport()); } catch { sb.AppendLine("(PerfTracker unavailable)"); }
        sb.AppendLine("--- log tail ---");
        try
        {
            var recent = Logger.GetRecent(8);
            if (recent.Count == 0) sb.AppendLine("(no logs)");
            else foreach (var e in recent) sb.AppendLine(e.Formatted.Length > 160 ? e.Formatted[..160] : e.Formatted);
        }
        catch { sb.AppendLine("(logger unavailable)"); }
        sb.Append("[~ / F3 toggle]");
        return sb.ToString();
    }

    private sealed partial class PerfGraph : Control
    {
        public override void _Draw()
        {
            try
            {
                var hist = PerfTracker.History;
                if (hist == null || hist.Count < 2) { DrawString(ThemeDB.FallbackFont, new Vector2(2, 12), "(no history)", HorizontalAlignment.Left, -1, 10, new Color(1, 1, 1, 0.6f)); return; }
                var arr = hist is PerfSample[] a ? a : new System.Collections.Generic.List<PerfSample>(hist).ToArray();
                float w = Size.X, h = Size.Y;
                DrawRect(new Rect2(0, 0, w, h), new Color(0, 0, 0, 0.55f));
                // scale by max tick 16ms (≈60fps budget) or observed max
                float max = 16f; foreach (var s in arr) if (s.TickMs > max) max = s.TickMs;
                if (max < 1f) max = 1f;
                Vector2 prev = default; bool first = true;
                for (int i = 0; i < arr.Length; i++)
                {
                    float x = i / (float)(arr.Length - 1) * w;
                    float y = h - Mathf.Clamp(arr[i].TickMs / max * h, 0, h);
                    var cur = new Vector2(x, y);
                    if (!first) DrawLine(prev, cur, new Color(0.3f, 0.9f, 0.55f), 1.2f);
                    first = false; prev = cur;
                }
                // 8ms guideline
                float y8 = h - 8f / max * h;
                if (y8 >= 0 && y8 <= h) DrawDashedLine(new Vector2(0, y8), new Vector2(w, y8), new Color(1, 1, 1, 0.25f), 1f, 2f);
            }
            catch { }
        }
    }
}
