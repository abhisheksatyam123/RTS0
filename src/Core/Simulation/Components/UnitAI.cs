/* Copyright (C) 2025 Wildfire Games.
 * Ported from binaries/data/mods/public/simulation/components/UnitAI.js (6652 lines)
 * and source/simulation2/components/CCmpUnitMotion*.cpp
 * 0 A.D. - GPLv2. Adapted for Godot 4.3 C# (RTS0) with deterministic Fixed math.
 *
 * This is a condensed, deterministic C# port of 0 A.D.'s UnitAI FSM.
 * Full JS uses dynamic prototype + Engine.QueryInterface; here we drive
 * Rts0.Core.SimUnit directly. Stances, order types and state names are kept
 * 1:1 so future diffing against 0ad stays trivial.
 */

using System;
using System.Collections.Generic;

namespace Rts0.Core;

/// <summary> Mirrors g_Stances in UnitAI.js:42-170 </summary>
public enum Stance { Violent, Aggressive, Defensive, Passive, StandGround, None }

public static class StanceConfig
{
    public record Config(
        bool TargetVisibleEnemies,
        bool TargetAttackersAlways,
        bool RespondFlee,
        bool RespondChase,
        bool RespondChaseBeyondVision,
        bool RespondStandGround,
        bool RespondHoldGround);

    public static readonly Dictionary<Stance, Config> Table = new()
    {
        [Stance.Violent]      = new(true,  true,  false,false,false,false,false),
        [Stance.Aggressive]   = new(true,  false, false,true, false,false,false),
        [Stance.Defensive]    = new(true,  false, false,false,false,false,true),
        [Stance.Passive]      = new(false, false, true, false,false,false,false),
        [Stance.StandGround]  = new(true,  false, false,false,false,true, false),
        [Stance.None]         = new(false, false, false,false,false,false,false),
    };
}

/// <summary>
/// Per-unit AI controller. One instance per SimUnit, ticked by GameSim.
/// Directly corresponds to UnitAI.js FSM; each public Order.* method maps to
/// "Order.Walk" etc in the JS file. States mirror "INDIVIDUAL.IDLE" etc.
/// </summary>
public sealed class UnitAI
{
    public readonly SimUnit Unit;
    public Stance CurrentStance = Stance.Aggressive;

    // Order queue — mirrors UnitAI.js orderQueue. Front = current order.
    private readonly Queue<GameCommand> _queue = new();
    public string CurrentState = "INDIVIDUAL.IDLE";
    public bool IsIdle => CurrentState == "INDIVIDUAL.IDLE";

    // Timers (in fixed ticks, not real seconds) — mirrors StartTimer(100) etc
    private int _timer;

    public UnitAI(SimUnit unit) => Unit = unit;

    public void PushOrder(GameCommand cmd) => _queue.Enqueue(cmd);
    public void PushOrderFront(GameCommand cmd)
    {
        var tmp = new Queue<GameCommand>();
        tmp.Enqueue(cmd);
        while (_queue.Count > 0) tmp.Enqueue(_queue.Dequeue());
        while (tmp.Count > 0) _queue.Enqueue(tmp.Dequeue());
    }
    public bool FinishOrder()
    {
        if (_queue.Count == 0) return false;
        _queue.Dequeue();
        SetNextState("INDIVIDUAL.IDLE");
        return _queue.Count == 0;
    }

    public void SetNextState(string state) => CurrentState = state;

    /// <summary> Called once per GameSim.Tick for each unit (parallel-safe: no cross-unit writes). </summary>
    public void Update(GameSim sim)
    {
        if (_timer > 0) { _timer--; return; }

        // Default handlers from UnitAI.js:42-250 (MovementUpdate etc ignored here)
        // Dispatch current order to state machine
        if (_queue.Count == 0)
        {
            // Auto-respond to nearby enemies per stance (mirrors LosRangeUpdate / Attacked)
            if (StanceConfig.Table[CurrentStance].TargetVisibleEnemies)
                TryAutoAttack(sim);
            return;
        }

        var order = _queue.Peek();
        bool accepted = order switch
        {
            MoveCommand m => OrderWalk(m, sim),
            BuildHouseCommand b => OrderBuild(b, sim),
            GatherCommand g => OrderGather(g, sim),
            AttackCommand a => OrderAttack(a, sim),
            _ => FinishOrder()
        };
        if (!accepted) FinishOrder();
    }

    // ---- Order handlers — names match JS Order.Walk etc ----

    private bool OrderWalk(MoveCommand cmd, GameSim sim)
    {
        if (!AbleToMove()) return false;
        // In 0ad: CheckRange -> FinishOrder if already in range; SetNextState WALKING
        if (IsInRange(cmd.Target, 0.5f)) return false;
        sim.AssignPath(Unit, cmd.Target);
        SetNextState("INDIVIDUAL.WALKING");
        return true;
    }

    private bool OrderBuild(BuildHouseCommand cmd, GameSim sim)
    {
        if (!sim.CanPlace(cmd.Cell, false)) return false;
        sim.AssignPath(Unit, new Godot.Vector3(cmd.Cell.X * GameSim.Grid, 0, cmd.Cell.Y * GameSim.Grid));
        SetNextState("INDIVIDUAL.BUILDING");
        return true;
    }

    private bool OrderGather(GatherCommand cmd, GameSim sim)
    {
        var res = sim.GetResource(cmd.ResourceId);
        if (res == null || res.Amount <= 0) return false;
        if (Unit.Carry >= 20) { SetNextState("INDIVIDUAL.GATHER.RETURNING"); return true; }
        if (IsInRange(res.Pos, 1.5f)) { SetNextState("INDIVIDUAL.GATHER.GATHERING"); return true; }
        sim.AssignPath(Unit, res.Pos);
        SetNextState("INDIVIDUAL.GATHER.APPROACHING");
        return true;
    }

    private bool OrderAttack(AttackCommand cmd, GameSim sim)
    {
        if (!sim.UnitById.TryGetValue(cmd.TargetId, out var target) || target.Hp <= 0) return false;
        if (IsInRange(target.Pos, Unit.AttackRange)) { SetNextState("INDIVIDUAL.COMBAT.ATTACKING"); return true; }
        if (!AbleToMove()) return false;
        sim.AssignPath(Unit, target.Pos);
        SetNextState("INDIVIDUAL.COMBAT.APPROACHING");
        return true;
    }

    // ---- State helpers (mirrors JS helpers) ----

    private bool AbleToMove() => true; // TODO: check UnitMotion.CanMove (obstruction)
    private bool IsInRange(Godot.Vector3 pos, float range) => Unit.Pos.DistanceTo(pos) < range;

    private void TryAutoAttack(GameSim sim)
    {
        // Mirrors RespondToSightedEntities — scan vision range (~12 tiles)
        SimUnit best = null; float bestDist = float.MaxValue;
        foreach (var u in sim.Units)
        {
            if (u.Team == Unit.Team || u.Hp <= 0) continue;
            float d = Unit.Pos.DistanceTo(u.Pos);
            if (d < 12f && d < bestDist) { bestDist = d; best = u; }
        }
        if (best != null && StanceConfig.Table[CurrentStance].RespondChase)
            PushOrder(new AttackCommand { PlayerId = 0, TargetId = best.Id });
    }

    public void StartTimer(int ticks) => _timer = ticks;
    public void StopTimer() => _timer = 0;
}
