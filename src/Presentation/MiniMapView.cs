using Godot;
using Rts0.Core;

// 0 A.D. inspiration: source/graphics/MiniMapTexture.cpp + LOSTexture/TerritoryTexture
// and gui/session/minimap/MiniMap.js — terrain→territory→LOS→icons blitted.
// RTS0 simplified: CanvasLayer Control with _Draw dots (no extra SubViewport render,
// keeps mobile draw calls low). Reuses RangeManager/TerritoryManager already ticking.
public partial class MiniMapView : Control
{
    private GameSim _sim;
    private Camera3D _cam;
    private const float MapSize = 60f; // GameSim.MapHalfExtent*2
    private const float MmSize = 148f;
    private const float Pad = 8f;

    public void Bind(GameSim sim, Camera3D cam) { _sim = sim; _cam = cam; }

    public override void _Ready()
    {
        CustomMinimumSize = new Vector2(MmSize, MmSize);
        Position = new Vector2(-MmSize - Pad, Pad);
        SetAnchorsPreset(LayoutPreset.TopRight, true);
        MouseFilter = MouseFilterEnum.Stop;
    }

    public override void _Process(double delta) => QueueRedraw();

    public override void _GuiInput(InputEvent @event)
    {
        if (@event is InputEventMouseButton mb && mb.Pressed && mb.ButtonIndex == MouseButton.Left)
        {
            Vector2 local = mb.Position;
            Vector2 world = new(
                Mathf.Lerp(-MapSize * 0.5f, MapSize * 0.5f, local.X / MmSize),
                Mathf.Lerp(-MapSize * 0.5f, MapSize * 0.5f, local.Y / MmSize));
            // move CameraRig (parent of _cam) — simple lerp like 0 A.D. flare
            var rig = _cam.GetParent<Node3D>();
            if (rig != null) rig.Position = new Vector3(world.X, rig.Position.Y, world.Y);
        }
    }

    public override void _Draw()
    {
        // background + border (0 A.D. panel style)
        DrawRect(new Rect2(0, 0, MmSize, MmSize), new Color(0.08f, 0.09f, 0.11f, 0.92f));
        DrawRect(new Rect2(0, 0, MmSize, MmSize), new Color(0.36f, 0.62f, 0.94f, 0.35f), false, 2f);

        if (_sim == null) return;

        // territory tint (simplified: single influence at origin, like GameSim._territoryManager)
        // 0 A.D. TerritoryTexture.GenerateBitmap blurs with alpha falloff — here a soft circle.
        DrawCircle(new Vector2(MmSize * 0.5f, MmSize * 0.5f), MmSize * 0.18f, new Color(0.36f, 0.62f, 0.94f, 0.14f));

        // resources: tree green / gold yellow (0 A.D. gaia icons)
        foreach (var r in _sim.Resources)
        {
            Vector2 p = WorldToMm(r.Pos);
            Color c = r.Type == ResourceType.Tree ? new Color(0.18f, 0.55f, 0.22f) : new Color(0.92f, 0.78f, 0.18f);
            DrawRect(new Rect2(p.X - 1.5f, p.Y - 1.5f, 3, 3), c);
        }

        // buildings: center vs house, blue/red by team
        foreach (var b in _sim.Buildings)
        {
            Vector2 p = WorldToMm(b.BasePos + new Vector3(b.SizeMeters * 0.5f, 0, b.SizeMeters * 0.5f));
            Color c = b.Team == Team.Player ? new Color(0.36f, 0.62f, 0.94f) : new Color(0.92f, 0.32f, 0.28f);
            float s = b.IsCenter ? 5f : 3.5f;
            DrawRect(new Rect2(p.X - s * 0.5f, p.Y - s * 0.5f, s, s), c);
            DrawRect(new Rect2(p.X - s * 0.5f, p.Y - s * 0.5f, s, s), new Color(1, 1, 1, 0.6f), false, 1f);
        }

        // units: small dots, hp alpha like 0 A.D. LOS fade
        foreach (var u in _sim.Units)
        {
            Vector2 p = WorldToMm(u.Pos);
            Color c = u.Team == Team.Player ? new Color(0.36f, 0.62f, 0.94f) : new Color(0.92f, 0.32f, 0.28f);
            float a = u.MaxHp > 0 ? (float)u.Hp / u.MaxHp : 1f;
            c.A = Mathf.Lerp(0.5f, 1f, a);
            DrawCircle(p, 1.8f, c);
        }

        // camera frustum rect (like 0 A.D. GameView minimap camera quad)
        if (_cam != null)
        {
            Vector2 camMm = WorldToMm(_cam.GlobalPosition);
            DrawRect(new Rect2(camMm.X - 10, camMm.Y - 10, 20, 20), new Color(1, 1, 1, 0.85f), false, 1f);
        }
    }

    private Vector2 WorldToMm(Vector3 w) =>
        new(Mathf.Clamp((w.X + MapSize * 0.5f) / MapSize * MmSize, 0, MmSize),
            Mathf.Clamp((w.Z + MapSize * 0.5f) / MapSize * MmSize, 0, MmSize));
}
