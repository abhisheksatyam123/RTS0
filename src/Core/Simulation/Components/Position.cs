/* Ported from source/simulation2/components/CCmpPosition.cpp + ICmpPosition.h — 0 A.D. GPL-2.0 */
/// <summary>Entity position/orientation — mirrors ICmpPosition + CCmpPosition.</summary>
using System.Collections.Generic;
using Godot;

namespace Rts0.Core;

public enum AnchorType { Upright = 0, Pitch = 1, Roll = 2, PitchRoll = 3 }

/// <summary>
/// Deterministic position component. Use FixedVector2D for simulation,
/// float for render interpolation. Attached to SimUnit via SimTypes.
/// Mirrors CCmpPosition API: GetPosition, SetPosition, MoveTo, IsInWorld.
/// </summary>
public sealed class Position : IComponent
{
    public int EntityId { get; private set; }
    private bool _inWorld;
    private FixedVector2D _pos, _prevPos, _lastPos;
    private Fixed _y; // altitude offset
    private bool _relativeToGround = true;
    private Fixed _rotX, _rotY, _rotZ;
    private Fixed _turnRate = Fixed.FromFloat(6.0f);
    private float _interpRotY;
    private AnchorType _anchor = AnchorType.Upright;
    private bool _floating;
    private Fixed _floatDepth;
    private Fixed _constructionProgress = Fixed.One;
    private int _turretParent = Player.InvalidPlayer;
    private FixedVector3D _turretOffset;
    private readonly HashSet<int> _turrets = new();

    public Position(int entityId) => EntityId = entityId;

    public void Init(Dictionary<string, object> p)
    {
        if (p != null && p.TryGetValue("Anchor", out var a)) _anchor = ParseAnchor(a.ToString());
        if (p != null && p.TryGetValue("Altitude", out var alt)) _y = Fixed.FromFloat(System.Convert.ToSingle(alt));
        if (p != null && p.TryGetValue("Floating", out var f)) _floating = System.Convert.ToBoolean(f);
        if (p != null && p.TryGetValue("FloatDepth", out var fd)) _floatDepth = Fixed.FromFloat(System.Convert.ToSingle(fd));
        if (p != null && p.TryGetValue("TurnRate", out var tr)) _turnRate = Fixed.FromFloat(System.Convert.ToSingle(tr));
    }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { w.Write(_inWorld); w.Write(_pos.X.RawValue); w.Write(_pos.Y.RawValue); w.Write(_y.RawValue); w.Write(_rotY.RawValue); }
    public void Deserialize(System.IO.BinaryReader r) { _inWorld = r.ReadBoolean(); _pos = new FixedVector2D(Fixed.FromRaw(r.ReadInt32()), Fixed.FromRaw(r.ReadInt32())); _y = Fixed.FromRaw(r.ReadInt32()); _rotY = Fixed.FromRaw(r.ReadInt32()); }

    // ---- core API mirroring ICmpPosition ----
    public bool IsInWorld() => _inWorld;
    public void MoveOutOfWorld() => _inWorld = false;
    public void SetPosition(Fixed x, Fixed z) => JumpTo(x, z);
    public void SetPosition(FixedVector2D p) => JumpTo(p.X, p.Y);
    public void MoveTo(Fixed x, Fixed z)
    {
        _pos = new FixedVector2D(x, z);
        if (!_inWorld) { _inWorld = true; _lastPos = _prevPos = _pos; }
    }
    public void MoveTo(FixedVector2D p) => MoveTo(p.X, p.Y);
    public void MoveAndTurnTo(Fixed x, Fixed z, Fixed ry) { MoveTo(x, z); TurnTo(ry); }
    public void JumpTo(Fixed x, Fixed z) { _prevPos = _lastPos = _pos = new FixedVector2D(x, z); _inWorld = true; }
    public void SetHeightOffset(Fixed dy) => _y += dy - GetHeightOffset();
    public Fixed GetHeightOffset() => _relativeToGround ? _y : _y; // simplified: no terrain query
    public void SetHeightFixed(Fixed y) => _y += y - GetHeightFixed();
    public Fixed GetHeightFixed() => _y; // simplified ground 0
    public Fixed GetHeightAtFixed(Fixed x, Fixed z) => _y;
    public bool IsHeightRelative() => _relativeToGround;
    public void SetHeightRelative(bool f) => _relativeToGround = f;
    public bool CanFloat() => _floating;
    public void SetFloating(bool f) => _floating = f;
    public void SetActorFloating(bool f) { }
    public void SetActorAnchor(string a) => _anchor = ParseAnchor(a);
    public void SetConstructionProgress(Fixed p) => _constructionProgress = p;

    public FixedVector3D GetPosition() => new(_pos.X, GetHeightFixed(), _pos.Y);
    public FixedVector2D GetPosition2D() => _pos;
    public FixedVector3D GetPreviousPosition() => new(_prevPos.X, _y, _prevPos.Y);
    public FixedVector2D GetPreviousPosition2D() => _prevPos;
    public Fixed GetTurnRate() => _turnRate;
    public void TurnTo(Fixed y) => _rotY = y;
    public void SetYRotation(Fixed y) { _rotY = y; _interpRotY = y.ToFloat(); }
    public void SetXZRotation(Fixed x, Fixed z) { _rotX = x; _rotZ = z; }
    public FixedVector3D GetRotation() => new(_rotX, _rotY, _rotZ);
    public Fixed GetDistanceTravelled() => (_pos - _lastPos).Length();

    // render helpers — float path
    public Vector3 GetPositionFloat() => new(_pos.X.ToFloat(), _y.ToFloat(), _pos.Y.ToFloat());
    public Vector3 GetInterpolatedPosition(float t)
    {
        float x = Mathf.Lerp(_lastPos.X.ToFloat(), _pos.X.ToFloat(), t);
        float z = Mathf.Lerp(_lastPos.Y.ToFloat(), _pos.Y.ToFloat(), t);
        return new Vector3(x, _y.ToFloat(), z);
    }
    public void GetInterpolatedPosition2D(float t, out float x, out float z, out float ry)
    {
        x = Mathf.Lerp(_lastPos.X.ToFloat(), _pos.X.ToFloat(), t);
        z = Mathf.Lerp(_lastPos.Y.ToFloat(), _pos.Y.ToFloat(), t);
        ry = _interpRotY;
    }

    // turret stubs
    public void SetTurretParent(int parent, FixedVector3D off) { _turretParent = parent; _turretOffset = off; }
    public int GetTurretParent() => _turretParent;
    public HashSet<int> GetTurrets() => _turrets;
    public void UpdateTurretPosition() { }

    // turn-start bookkeeping — call from GameSim.Tick
    public void OnTurnStart() { _prevPos = _lastPos; _lastPos = _pos; }

    // sync to SimUnit helper
    public void SyncToSimUnit(SimUnit u)
    {
        if (!_inWorld) return;
        u.Pos = GetPositionFloat();
        // store prev for interpolation
        u.PrevPos = new Vector3(_prevPos.X.ToFloat(), _y.ToFloat(), _prevPos.Y.ToFloat());
    }
    public void SyncFromSimUnit(SimUnit u)
    {
        MoveTo(Fixed.FromFloat(u.Pos.X), Fixed.FromFloat(u.Pos.Z));
        _y = Fixed.FromFloat(u.Pos.Y);
    }

    private static AnchorType ParseAnchor(string s) => s switch
    {
        "pitch" => AnchorType.Pitch,
        "roll" => AnchorType.Roll,
        "pitch-roll" => AnchorType.PitchRoll,
        _ => AnchorType.Upright
    };
}
