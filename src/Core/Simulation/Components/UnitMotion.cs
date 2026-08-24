/* Ported from source/simulation2/components/CCmpUnitMotion.h/.cpp + CCmpUnitMotionManager.h — 0 A.D. GPL-2.0 */
/// <summary>Deterministic unit motion — mirrors ICmpUnitMotion / CCmpUnitMotion.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class UnitMotion : IComponent
{
    public int EntityId { get; private set; }
    public void Init(Dictionary<string, object> p) { }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { w.Write(_walkSpeed.RawValue); w.Write(_speed.RawValue); }
    public void Deserialize(System.IO.BinaryReader r) { _walkSpeed = Fixed.FromRaw(r.ReadInt32()); _speed = Fixed.FromRaw(r.ReadInt32()); }

    private Fixed _walkSpeed = Fixed.FromInt(7);
    private Fixed _runMultiplier = Fixed.One;
    private Fixed _acceleration = Fixed.FromInt(10);
    private Fixed _clearance = Fixed.FromInt(1);
    private ushort _passClass = 1;
    private string _passClassName = "default";
    private Fixed _speed, _currentSpeed;
    private Fixed _speedMult = Fixed.One;
    private Fixed _weight = Fixed.FromInt(10);
    private bool _faceAfterMove = true;
    private bool _isFormationController;

    private struct MoveRequest
    {
        public enum Type { None, Point, Target, Offset }
        public Type Kind; public FixedVector2D Pos; public int Target; public Fixed MinRange, MaxRange; public FixedVector2D Offset;
    }
    private MoveRequest _request;
    private int _formationController = Player.InvalidPlayer;
    private uint _ticket; private bool _waiting;
    private WaypointPath _longPath = new(), _shortPath = new();
    private byte _failedMoves;
    private const int ShortMin = 12, ShortMax = 28;

    public UnitMotion(int id) { EntityId = id; _speed = _walkSpeed; }

    public Fixed GetWalkSpeed() => _walkSpeed;
    public Fixed GetRunMultiplier() => _runMultiplier;
    public Fixed GetSpeed() => _speed;
    public Fixed GetSpeedMultiplier() => _speedMult;
    public void SetSpeedMultiplier(Fixed m) { _speedMult = m > _runMultiplier ? _runMultiplier : m; _speed = _walkSpeed.Multiply(_speedMult); }
    public Fixed GetAcceleration() => _acceleration;
    public void SetAcceleration(Fixed a) => _acceleration = a;
    public Fixed GetCurrentSpeed() => _currentSpeed;
    public ushort GetPassabilityClass() => _passClass;
    public string GetPassabilityClassName() => _passClassName;
    public void SetPassabilityClassName(string n) => _passClassName = n;
    public Fixed GetUnitClearance() => _clearance;
    public void SetFacePointAfterMove(bool v) => _faceAfterMove = v;
    public bool GetFacePointAfterMove() => _faceAfterMove;
    public bool IsMoveRequested() => _request.Kind != MoveRequest.Type.None;
    public void SetDebugOverlay(bool v) { }

    public bool MoveToPoint(Fixed x, Fixed z) => MoveToPointRange(x, z, Fixed.Zero, Fixed.Zero);
    public void Stop() => StopMoving();
    public bool MoveToPointRange(Fixed x, Fixed z, Fixed minRange, Fixed maxRange)
    {
        _request = new MoveRequest { Kind = MoveRequest.Type.Point, Pos = new FixedVector2D(x, z), MinRange = minRange, MaxRange = maxRange };
        _longPath.Clear(); _shortPath.Clear(); _failedMoves = 0;
        return true;
    }
    public bool MoveToTargetRange(int target, Fixed minRange, Fixed maxRange)
    {
        _request = new MoveRequest { Kind = MoveRequest.Type.Target, Target = target, MinRange = minRange, MaxRange = maxRange };
        _longPath.Clear(); _shortPath.Clear(); return true;
    }
    public void MoveToFormationOffset(int controller, Fixed x, Fixed z)
    {
        _request = new MoveRequest { Kind = MoveRequest.Type.Offset, Target = controller, Offset = new FixedVector2D(x, z) };
    }
    public void SetMemberOfFormation(int controller) => _formationController = controller;
    public bool IsTargetRangeReachable(int target, Fixed minRange, Fixed maxRange) => true;
    public bool PossiblyAtDestination() => _request.Kind == MoveRequest.Type.None;
    public void FaceTowardsPoint(Fixed x, Fixed z) { }
    public void StopMoving()
    {
        _request = default; _longPath.Clear(); _shortPath.Clear(); _waiting = false;
    }
    public FixedVector2D EstimateFuturePosition(Fixed dt) => new FixedVector2D(Fixed.Zero, Fixed.Zero);

    // called each GameSim tick — returns new pos (caller writes to Position)
    public bool Tick(Position posComp, Pathfinder pathfinder, Fixed dt)
    {
        if (!posComp.IsInWorld() || _request.Kind == MoveRequest.Type.None) return false;
        FixedVector2D cur = posComp.GetPosition2D();
        FixedVector2D goal = ResolveGoal(cur);
        if (NeedRepath(cur, goal)) ComputePath(cur, goal, pathfinder);
        return PerformMove(cur, posComp, dt, pathfinder);
    }

    private FixedVector2D ResolveGoal(FixedVector2D cur)
    {
        return _request.Kind switch
        {
            MoveRequest.Type.Point => _request.Pos,
            MoveRequest.Type.Target => _request.Pos, // simplified: assume pos cached
            MoveRequest.Type.Offset => _request.Offset,
            _ => cur
        };
    }
    private bool NeedRepath(FixedVector2D cur, FixedVector2D goal)
    {
        if (_longPath.Empty && _shortPath.Empty) return true;
        // repath if far from goal and no path pending
        return (goal - cur).Length() > Fixed.FromInt(2) && _longPath.Empty && _shortPath.Empty && !_waiting;
    }
    private void ComputePath(FixedVector2D from, FixedVector2D goal, Pathfinder pf)
    {
        if (pf == null) { _longPath.Waypoints.Add(new Waypoint(goal.X, goal.Y)); return; }
        var g = PathGoal.Point(goal.X, goal.Y);
        if ((goal - from).Length() < Fixed.FromInt(16))
        {
            var req = new ShortPathRequest { X0 = from.X, Z0 = from.Y, Goal = g, Clearance = _clearance, Range = Fixed.FromInt(ShortMin), PassClass = _passClass };
            _shortPath = pf.ComputeShortPathImmediate(req);
        }
        else
        {
            var ret = new WaypointPath(); pf.ComputePathImmediate(from.X, from.Y, g, _passClass, ret); _longPath = ret;
        }
    }
    private bool PerformMove(FixedVector2D cur, Position posComp, Fixed dt, Pathfinder pf)
    {
        WaypointPath path = !_shortPath.Empty ? _shortPath : _longPath;
        if (path.Empty) return false;
        FixedVector2D target = new(path.Waypoints[^1].X, path.Waypoints[^1].Z);
        FixedVector2D dir = target - cur;
        Fixed len = dir.Length();
        if (len.IsZero()) { path.Waypoints.RemoveAt(path.Waypoints.Count - 1); return true; }
        Fixed maxDist = _speed.Multiply(dt);
        // accel
        _currentSpeed = _currentSpeed + _acceleration.Multiply(dt);
        if (_currentSpeed > _speed) _currentSpeed = _speed;
        if (len <= maxDist)
        {
            posComp.MoveTo(target.X, target.Y);
            path.Waypoints.RemoveAt(path.Waypoints.Count - 1);
            return true;
        }
        FixedVector2D step = new(dir.X.MulDiv(maxDist, len), dir.Y.MulDiv(maxDist, len));
        posComp.MoveTo(cur.X + step.X, cur.Y + step.Y);
        return true;
    }
}
