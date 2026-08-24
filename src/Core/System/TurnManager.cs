/* Ported from source/simulation2/system/TurnManager.h/.cpp + LocalTurnManager.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>Turn manager — mirrors CTurnManager / CLocalTurnManager (source/simulation2/system/TurnManager.h/.cpp, LocalTurnManager.h/.cpp).</summary>
/// <remarks>Reference: TurnManager.h, TurnManager.cpp, LocalTurnManager.h/.cpp — DEFAULT_TURN_LENGTH, COMMAND_DELAY</remarks>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public abstract class TurnManager
{
    public const uint DefaultTurnLength = 200;
    public const uint CommandDelaySP = 1;
    public const uint CommandDelayMP = 4;

    protected uint _currentTurn;
    protected uint _readyTurn;
    protected uint _turnLength;
    protected uint _commandDelay;
    protected float _deltaSimTime;
    protected int _playerId = -1;
    protected uint _clientId;
    protected readonly List<Dictionary<uint, List<GameCommand>>> _queued = new();

    protected TurnManager(uint defaultLen, uint cmdDelay, uint clientId)
    {
        _turnLength = defaultLen; _commandDelay = cmdDelay; _clientId = clientId;
        _readyTurn = cmdDelay - 1; _queued.Add(new());
    }

    public uint GetCurrentTurn() => _currentTurn;
    public uint GetPendingTurns() => _readyTurn - _currentTurn;
    public void SetPlayerID(int id) => _playerId = id;
    public void ResetState(uint cur, uint ready) { _currentTurn = cur; _readyTurn = ready; _deltaSimTime = 0; _queued.Clear(); _queued.Add(new()); }
    public void FinishedAllCommands(uint turn, uint len) { if (turn == _readyTurn + 1) { _readyTurn = turn; _turnLength = len; } }

    protected void AddCommand(uint client, int player, GameCommand cmd, uint turn)
    {
        if (_currentTurn >= turn) return;
        uint idx = turn - (_currentTurn + 1);
        while (_queued.Count <= idx) _queued.Add(new());
        if (!_queued[(int)idx].TryGetValue(client, out var list)) _queued[(int)idx][client] = list = new();
        list.Add(cmd);
    }

    public bool Update(float simFrameLen, int maxTurns, Action<uint, IReadOnlyList<GameCommand>> doTurn)
    {
        _deltaSimTime += simFrameLen;
        _deltaSimTime = Math.Min(_deltaSimTime, 2f * _turnLength / 1000f);
        if (_deltaSimTime < 0) return false;
        if (_readyTurn <= _currentTurn && _commandDelay > 1) { _deltaSimTime = 0; return false; }
        maxTurns = Math.Max(1, maxTurns);
        bool did = false;
        for (int i = 0; i < maxTurns; i++)
        {
            if (_deltaSimTime < 0) break;
            if (_readyTurn <= _currentTurn && _commandDelay > 1) break;
            NotifyFinishedOwnCommands(_currentTurn + _commandDelay);
            _currentTurn++;
            var cmds = new List<GameCommand>();
            if (_queued.Count > 0) { foreach (var kv in _queued[0]) cmds.AddRange(kv.Value); _queued.RemoveAt(0); _queued.Add(new()); }
            doTurn(_currentTurn, cmds);
            NotifyFinishedUpdate(_currentTurn);
            _deltaSimTime -= _turnLength / 1000f;
            did = true;
        }
        return did;
    }

    public bool UpdateFastForward(Action<uint, IReadOnlyList<GameCommand>> doTurn)
    {
        if (_readyTurn <= _currentTurn) return false;
        while (_readyTurn > _currentTurn)
        {
            _currentTurn++;
            var cmds = new List<GameCommand>();
            if (_queued.Count > 0) { foreach (var kv in _queued[0]) cmds.AddRange(kv.Value); _queued.RemoveAt(0); _queued.Add(new()); }
            doTurn(_currentTurn, cmds);
        }
        return true;
    }

    public float Interpolate(float simFrameLen) => Math.Clamp(_deltaSimTime / (_turnLength / 1000f) + 1f, 0f, 1f);

    public abstract void OnSimulationMessage(object? msg);
    public abstract void PostCommand(GameCommand cmd);
    protected abstract void NotifyFinishedOwnCommands(uint turn);
    protected abstract void NotifyFinishedUpdate(uint turn);
}

public sealed class LocalTurnManager : TurnManager
{
    public LocalTurnManager() : base(DefaultTurnLength, CommandDelaySP, 0) { }
    public override void OnSimulationMessage(object? msg) { }
    public override void PostCommand(GameCommand cmd) => AddCommand(_clientId, cmd.PlayerId, cmd, _currentTurn + _commandDelay);
    public void PostCommand(int playerId, GameCommand cmd) => AddCommand(_clientId, playerId, cmd, _currentTurn + _commandDelay);
    protected override void NotifyFinishedOwnCommands(uint turn) => FinishedAllCommands(turn, _turnLength);
    protected override void NotifyFinishedUpdate(uint turn) { }
}
