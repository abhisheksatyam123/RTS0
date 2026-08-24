/// Ported from source/simulation2/helpers/Player.h — 0 A.D. GPL-2.0
// Shim: actual Player helper lives in Simulation/Player.cs (shared namespace Rts0.Core).
// This file ensures src/Core/Helpers/Player.cs exists as requested.

using System;

namespace Rts0.Core;

// Player static class is defined in Simulation/Player.cs; no duplicate here.
// This helper provides alias constants for helpers import path.
public static class PlayerHelperAlias
{
    public const int InvalidPlayer = Player.InvalidPlayer;
    public const int GaiaPlayer = Player.Gaia;
}
