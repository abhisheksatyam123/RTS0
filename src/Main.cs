using System.Collections.Generic;
using System.Linq;
using Godot;
using Rts0.Core;
using Rts0.Presentation;

// Thin glue layer: input gestures -> serializable commands -> GameSim.
// Never touches sim data directly; rendering goes through WorldView.
public partial class Main : Node3D
{
    private const float DragThreshold = 8.0f;
    private const float PickRadiusPx = 26.0f;

    private readonly GameSim _sim = new();
    private WorldView _view;

    private Camera3D _cam;
    private SelectionRect _selRect;
    private Label _hud;
    private MeshInstance3D _ghost;
    private StandardMaterial3D _ghostMatOk;
    private StandardMaterial3D _ghostMatBad;

    private bool _buildMode;
    private readonly HashSet<int> _selectedIds = new();

    private bool _dragging;
    private Vector2 _dragStart;
    private bool _clickedEmpty;
    private double _accumulator;
    private int _lastHudGold = -1;
    private int _lastHudUnits = -1;

    public override void _Ready()
    {
        // logging + perf + crash — must be first, before any sim tick can throw
        Logger.Init("RTS0");
        CrashHandler.Install(() => $"Gold={_sim.Gold} Wave={_sim.WaveNumber} Units={_sim.Units.Count} Buildings={_sim.Buildings.Count}");
        BugReporter.SetSnapshotHook(() => $"Gold={_sim.Gold} Wave={_sim.WaveNumber} Units={_sim.Units.Count}");
        Logger.Info("Main", $"RTS0 _Ready — Godot {Engine.GetVersionInfo()["string"]} {OS.GetName()} {OS.GetModelName()} Vulkan={RenderingServer.GetVideoAdapterName()}");
        var debugOverlay = new DebugOverlay();
        AddChild(debugOverlay);

        BuildWorld();
        BuildUi();
        _sim.SpawnInitial();
        _view = new WorldView();
        AddChild(_view);
        Logger.Info("Main", $"Spawned — Gold={_sim.Gold} Units={_sim.Units.Count} Buildings={_sim.Buildings.Count} Resources={_sim.Resources.Count}");
    }

    public override void _Process(double delta)
    {
        PerfTracker.BeginFrame();
        long tickStart = Time.GetTicksMsec();
        // Fixed-timestep simulation, render decoupled with interpolation.
        _accumulator += delta;
        int ticks = 0;
        while (_accumulator >= GameSim.FixedStep)
        {
            _sim.Tick(GameSim.FixedStep);
            _accumulator -= GameSim.FixedStep;
            ticks++;
        }
        if (ticks > 0) PerfTracker.RecordTick(Time.GetTicksMsec() - tickStart);
        long drawStart = Time.GetTicksMsec();
        _view.Sync(_sim, (float)(_accumulator / GameSim.FixedStep));
        PerfTracker.RecordDraw(_sim.Units.Count, _sim.Buildings.Count);
        if (Engine.GetFramesDrawn() % 300 == 0) // every ~5s
            Logger.Debug("Perf", PerfTracker.GetReport());

        UpdateHud();

        if (_buildMode && _ghost != null)
        {
            Vector2I cell = MouseCell(GetViewport().GetMousePosition());
            Vector3 center = CellCenter(cell);
            _ghost.GlobalPosition = center + Vector3.Up * 0.9f;
            bool valid = _sim.Gold >= GameSim.HouseCost && _sim.CanPlace(cell, isCenter: false);
            _ghost.MaterialOverride = valid ? _ghostMatOk : _ghostMatBad;
        }
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is InputEventKey key && key.Pressed && !key.Echo)
        {
            if (key.PhysicalKeycode == Key.B)
            {
                EnterBuildMode(!_buildMode);
                return;
            }
            if (key.PhysicalKeycode == Key.Escape)
            {
                if (_buildMode) EnterBuildMode(false);
                else ClearSelection();
                return;
            }
        }

        if (@event is InputEventMouseButton mb)
        {
            if (mb.ButtonIndex == MouseButton.Left && mb.Pressed)
            {
                if (_buildMode)
                {
                    TryPlace();
                    return;
                }
                _dragging = true;
                _dragStart = mb.Position;
                _selRect.SetRect(new Rect2(_dragStart, Vector2.Zero), true);

                SimUnit picked = PickUnit(mb.Position);
                _clickedEmpty = picked == null;
                ClearSelection();
                if (picked != null) Select(picked.Id);
            }
            else if (mb.ButtonIndex == MouseButton.Left && !mb.Pressed && _dragging)
            {
                _dragging = false;
                _selRect.SetRect(new Rect2(), false);
                Rect2 rect = new Rect2(_dragStart, mb.Position - _dragStart).Abs();
                if (rect.Size.Length() > DragThreshold) SelectBox(rect);
                else if (_clickedEmpty) ClearSelection();
            }
            else if (mb.ButtonIndex == MouseButton.Right && mb.Pressed)
            {
                if (_buildMode) EnterBuildMode(false);
                else
                {
                    var enemy = PickEnemyUnit(mb.Position);
                    if (enemy != null) IssueAttack(enemy.Id);
                    else
                    {
                        var res = PickResource(mb.Position);
                        if (res != null) IssueGather(res.Id);
                        else IssueMove(MouseGround(mb.Position));
                    }
                }
            }
        }
        else if (@event is InputEventMouseMotion motion && _dragging)
        {
            _selRect.SetRect(new Rect2(_dragStart, motion.Position - _dragStart).Abs(), true);
        }
    }

    // ---------- world / ui setup ----------

    private void BuildWorld()
    {
        var worldEnv = new WorldEnvironment
        {
            Environment = new Environment
            {
                BackgroundMode = Environment.BGMode.Sky,
                Sky = new Sky { SkyMaterial = new ProceduralSkyMaterial() },
                AmbientLightSource = Environment.AmbientSource.Sky,
                TonemapMode = Environment.ToneMapper.Filmic,
            },
        };
        AddChild(worldEnv);

        var sun = new DirectionalLight3D
        {
            RotationDegrees = new Vector3(-52.0f, -35.0f, 0.0f),
            ShadowEnabled = true,
        };
        AddChild(sun);

        var grass = new StandardMaterial3D
        {
            AlbedoColor = new Color(0.44f, 0.58f, 0.34f),
            Roughness = 1.0f,
        };
        AddChild(new MeshInstance3D
        {
            Mesh = new PlaneMesh { Size = new Vector2(64.0f, 64.0f), Material = grass },
        });

        var rig = new CameraRig { Name = "CameraRig", Position = new Vector3(0.0f, 0.0f, 8.0f) };
        var cam3d = new Camera3D
        {
            Name = "Cam",
            RotationDegrees = new Vector3(-52.0f, 0.0f, 0.0f),
            Fov = 45.0f,
        };
        rig.AddChild(cam3d);
        AddChild(rig);
        cam3d.Current = true;
        _cam = cam3d;
    }

    private void BuildUi()
    {
        var uiLayer = new CanvasLayer { Name = "UI" };
        AddChild(uiLayer);

        _selRect = new SelectionRect();
        _selRect.SetAnchorsPreset(Control.LayoutPreset.FullRect);
        _selRect.MouseFilter = Control.MouseFilterEnum.Ignore;
        uiLayer.AddChild(_selRect);

        _hud = new Label { Position = new Vector2(12.0f, 10.0f) };
        _hud.AddThemeFontSizeOverride("font_size", 18);
        uiLayer.AddChild(_hud);
        UpdateHud(true);

        _ghostMatOk = new StandardMaterial3D
        {
            AlbedoColor = new Color(0.3f, 0.9f, 1.0f, 0.4f),
            Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
        };
        _ghostMatBad = new StandardMaterial3D
        {
            AlbedoColor = new Color(1.0f, 0.35f, 0.3f, 0.45f),
            Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
        };
    }

    // ---------- input helpers ----------

    private Vector3 MouseGround(Vector2 mousePos)
    {
        Vector3 origin = _cam.ProjectRayOrigin(mousePos);
        Vector3 dir = _cam.ProjectRayNormal(mousePos);
        if (Mathf.Abs(dir.Y) < 0.0001f) return origin + dir * 100.0f;
        float t = -origin.Y / dir.Y;
        if (t < 0.0f) return origin + dir * 100.0f;
        return origin + dir * t;
    }

    private Vector2I MouseCell(Vector2 mousePos)
    {
        Vector3 ground = MouseGround(mousePos);
        return new Vector2I(
            Mathf.FloorToInt(ground.X / GameSim.Grid),
            Mathf.FloorToInt(ground.Z / GameSim.Grid));
    }

    private static Vector3 CellCenter(Vector2I cell) =>
        new((cell.X + 0.5f) * GameSim.Grid, 0.0f, (cell.Y + 0.5f) * GameSim.Grid);

    private SimUnit PickUnit(Vector2 mousePos)
    {
        SimUnit best = null;
        float bestDist = PickRadiusPx;
        foreach (SimUnit unit in _sim.Units)
        {
            if (unit.Team != Team.Player) continue;
            Vector2 screenPos = _cam.UnprojectPosition(unit.Pos + Vector3.Up * 0.6f);
            float dist = screenPos.DistanceTo(mousePos);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = unit;
            }
        }
        return best;
    }

    private SimUnit PickEnemyUnit(Vector2 mousePos)
    {
        SimUnit best = null;
        float bestDist = PickRadiusPx;
        foreach (SimUnit unit in _sim.Units)
        {
            if (unit.Team != Team.Enemy) continue;
            Vector2 screenPos = _cam.UnprojectPosition(unit.Pos + Vector3.Up * 0.6f);
            float dist = screenPos.DistanceTo(mousePos);
            if (dist < bestDist) { bestDist = dist; best = unit; }
        }
        return best;
    }

    private SimResource PickResource(Vector2 mousePos)
    {
        SimResource best = null;
        float bestDist = PickRadiusPx * 1.2f;
        foreach (SimResource res in _sim.Resources)
        {
            Vector2 screenPos = _cam.UnprojectPosition(res.Pos + Vector3.Up * 0.8f);
            float dist = screenPos.DistanceTo(mousePos);
            if (dist < bestDist) { bestDist = dist; best = res; }
        }
        return best;
    }

    // ---------- selection ----------

    private void Select(int unitId)
    {
        _selectedIds.Add(unitId);
        _view.SetSelected(unitId, true);
    }

    private void ClearSelection()
    {
        foreach (int id in _selectedIds) _view.SetSelected(id, false);
        _selectedIds.Clear();
    }

    private void SelectBox(Rect2 rect)
    {
        ClearSelection();
        foreach (SimUnit unit in _sim.Units)
        {
            if (unit.Team != Team.Player) continue;
            Vector2 screenPos = _cam.UnprojectPosition(unit.Pos + Vector3.Up * 0.6f);
            if (rect.HasPoint(screenPos)) Select(unit.Id);
        }
    }

    // ---------- commands (routed through UnitAI queues for 0 A.D. determinism) ----------

    private void IssueMove(Vector3 worldPos)
    {
        if (_selectedIds.Count == 0) return;
        _sim.Apply(new MoveCommand
        {
            PlayerId = 0,
            UnitIds = _selectedIds.ToArray(),
            Target = worldPos,
        });
    }

    private void IssueGather(int resourceId)
    {
        if (_selectedIds.Count == 0) return;
        _sim.OrderGather(_selectedIds.ToArray(), resourceId);
    }

    private void IssueAttack(int targetId)
    {
        if (_selectedIds.Count == 0) return;
        _sim.OrderAttack(_selectedIds.ToArray(), targetId);
    }

    private void TryPlace()
    {
        _sim.Apply(new BuildHouseCommand
        {
            PlayerId = 0,
            Cell = MouseCell(GetViewport().GetMousePosition()),
        });
        if (!Input.IsPhysicalKeyPressed(Key.Shift)) EnterBuildMode(false);
    }

    private void EnterBuildMode(bool enabled)
    {
        _buildMode = enabled;
        if (enabled)
        {
            ClearSelection();
            _ghost = new MeshInstance3D
            {
                Mesh = new BoxMesh { Size = new Vector3(GameSim.Grid, 1.8f, GameSim.Grid) },
                MaterialOverride = _ghostMatOk,
            };
            AddChild(_ghost);
        }
        else
        {
            _ghost?.QueueFree();
            _ghost = null;
        }
    }

    private void UpdateHud(bool force = false)
    {
        int unitCount = _sim.Units.Count;
        if (!force && unitCount == _lastHudUnits && _sim.Gold == _lastHudGold) return;
        _lastHudGold = _sim.Gold;
        _lastHudUnits = unitCount;
        int playerUnits = 0, enemies = 0;
        foreach (var u in _sim.Units) if (u.Team == Team.Player) playerUnits++; else enemies++;
        _hud.Text =
            $"Gold: {_sim.Gold}  Wave: {_sim.WaveNumber}  You: {playerUnits}  Enemies: {enemies}\n" +
            "WASD pan - Q/E rotate - Wheel zoom\n" +
            $"LMB select - RMB gather/attack/move - B build (cost {GameSim.HouseCost})";
    }
}
