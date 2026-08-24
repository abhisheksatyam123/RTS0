using Godot;

public partial class CameraRig : Node3D
{
    [Export] public float PanSpeed { get; set; } = 16.0f;
    [Export] public float RotateSpeed { get; set; } = 120.0f;
    [Export] public float MinHeight { get; set; } = 10.0f;
    [Export] public float MaxHeight { get; set; } = 30.0f;
    [Export] public float MapLimit { get; set; } = 28.0f;

    private float _height = 20.0f;

    private Camera3D _cam;

    public override void _Ready()
    {
        _cam = GetNode<Camera3D>("Cam");
        ApplyHeight();
    }

    private void ApplyHeight()
    {
        _cam.Position = new Vector3(0.0f, _height, _height * 0.85f);
    }

    public override void _Process(double delta)
    {
        var dir = Vector3.Zero;
        if (Input.IsPhysicalKeyPressed(Key.W)) dir -= Transform.Basis.Z;
        if (Input.IsPhysicalKeyPressed(Key.S)) dir += Transform.Basis.Z;
        if (Input.IsPhysicalKeyPressed(Key.A)) dir -= Transform.Basis.X;
        if (Input.IsPhysicalKeyPressed(Key.D)) dir += Transform.Basis.X;

        if (dir.LengthSquared() > 0.0f)
        {
            Position += dir.Normalized() * PanSpeed * (float)delta;
            Position = new Vector3(
                Mathf.Clamp(Position.X, -MapLimit, MapLimit),
                Position.Y,
                Mathf.Clamp(Position.Z, -MapLimit, MapLimit));
        }

        if (Input.IsPhysicalKeyPressed(Key.Q)) RotateY(Mathf.DegToRad(RotateSpeed * (float)delta));
        if (Input.IsPhysicalKeyPressed(Key.E)) RotateY(-Mathf.DegToRad(RotateSpeed * (float)delta));
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is not InputEventMouseButton mb || !mb.Pressed) return;

        if (mb.ButtonIndex == MouseButton.WheelUp)
        {
            _height = Mathf.Clamp(_height - 2.0f, MinHeight, MaxHeight);
            ApplyHeight();
        }
        else if (mb.ButtonIndex == MouseButton.WheelDown)
        {
            _height = Mathf.Clamp(_height + 2.0f, MinHeight, MaxHeight);
            ApplyHeight();
        }
    }
}
