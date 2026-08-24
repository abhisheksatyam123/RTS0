/// Ported from Trainer.js, TrainingRestrictions.js, UnitMotionFlying.js — 0 A.D. GPL-2.0
using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary>Trainer — mirrors Trainer.js.</summary>
public sealed class TrainerComponent
{
    public float BatchTimeModifier = 1f;
    public HashSet<string> Trainable = new();
    public sealed class Item
    {
        public int Count; public string TemplateName = ""; public int Trainer; public object Metadata;
        public Dictionary<string,float> Resources = new(); public int Population; public bool Paid, Started, Finished;
        public List<int> Entities = new(); public float TimeRemaining, TimeTotal; public int Player;
        public bool Queue(Dictionary<string,float> mult, float batchMult, Func<string, Dictionary<string,float>> getCosts)
        {
            if (Count <= 0) return false;
            var costs = getCosts(TemplateName); if (costs == null) return false;
            Resources = costs; TimeRemaining = TimeTotal = 1000 * batchMult; return true;
        }
        public void Stop(Action<int> refund) { foreach (var e in Entities) refund(e); Entities.Clear(); }
        public bool Start(Func<int,bool> reserve) => reserve(Population * Count);
        public bool IsFinished() => Finished;
        public void Finish() { Finished = Count == 0; }
        public void Spawn(Func<string,int> add, Action<int> place) { while (Entities.Count > 0) { place(Entities[0]); Entities.RemoveAt(0); } }
    }
    public string[] GetTrainableEntities() => new List<string>(Trainable).ToArray();
    public bool CanTrain(string tmpl) => Trainable.Contains(tmpl);
}

/// <summary>Training restrictions — mirrors TrainingRestrictions.js.</summary>
public sealed class TrainingRestrictionsComponent
{
    public string Category = "Infantry";
    public int? MatchLimit;
    public string GetCategory() => Category;
}

/// <summary>Flying motion — mirrors UnitMotionFlying.js (fun, JS perf demo).</summary>
public sealed class UnitMotionFlyingComponent
{
    public float MaxSpeed = 12f, TakeoffSpeed = 8f, LandingSpeed = 4f, AccelRate = 4f, SlowingRate = 6f, BrakingRate = 10f, TurnRate = 1.5f, OvershootTime = 1f, FlyingHeight = 20f, ClimbRate = 10f;
    public bool DiesInWater; public string PassabilityClass = "flying";
    private bool _hasTarget, _reached, _landing, _onGround = true, _waterDeath;
    private float _targetX, _targetZ, _minRange, _maxRange, _speed, _pitch, _roll;
    private int _passClass;
    public bool MoveToPointRange(float x, float z, float min, float max) { _hasTarget = true; _landing = false; _reached = false; _targetX = x; _targetZ = z; _minRange = min; _maxRange = max; return true; }
    public bool MoveToTargetRange(Vector3D pos, float min, float max) => MoveToPointRange(pos.X, pos.Z, min, max);
    public bool PossiblyAtDestination() => _reached;
    public void SetMemberOfFormation() { }
    public float GetWalkSpeed() => MaxSpeed;
    public void SetSpeedMultiplier(float m) { }
    public float GetRunMultiplier() => 1f;
    public Vector2D EstimateFuturePosition(Vector2D pos, Vector2D prev, float dt, float turnLen) => pos + (pos - prev) * (dt / Math.Max(turnLen, 1e-6f));
    public bool IsMoveRequested() => _hasTarget;
    public float GetCurrentSpeed() => _speed;
    public float GetSpeedMultiplier() => MaxSpeed > 0 ? _speed / MaxSpeed : 0;
    public float GetAcceleration() => AccelRate;
    public void SetAcceleration(float a) { }
    public string GetPassabilityClassName() => PassabilityClass;
    public void SetPassabilityClassName(string s, Func<string,int> get) { PassabilityClass = s; _passClass = get(s); }
    public int GetPassabilityClass() => _passClass;
    public void FaceTowardsPoint(float x, float z) { }
    public void SetFacePointAfterMove(bool v) { }
    public void StopMoving() { if (!_waterDeath) _landing = !_landing; }
    public void SetDebugOverlay(bool e) { }
    public void OnUpdate(float dt, Vector3D cur, float yaw, Func<float,float,float> ground, Func<float,float,float> water, Action<Vector3D,float,float,float> apply)
    {
        if (!_hasTarget) return;
        float dx = _targetX - cur.X, dz = _targetZ - cur.Z; float dist2 = dx * dx + dz * dz;
        if (_landing)
        {
            if (_speed > 0 && _onGround) { _speed = Math.Max(0, _speed - dt * BrakingRate * (_waterDeath ? 10 : 1)); _pitch = 0; }
            else if (_speed == 0 && _onGround) { _hasTarget = false; _landing = false; }
            else { _speed = Math.Max(LandingSpeed, _speed - dt * SlowingRate); }
        }
        else { _speed = Math.Min(MaxSpeed, _speed + dt * AccelRate); }
        if (!_reached && _minRange * _minRange <= dist2 && dist2 <= _maxRange * _maxRange) _reached = true;
        float nx = cur.X + _speed * dt * MathF.Sin(yaw), nz = cur.Z + _speed * dt * MathF.Cos(yaw);
        apply(new Vector3D(nx, cur.Y, nz), yaw, _pitch, _roll);
    }
}
