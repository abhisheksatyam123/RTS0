#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <utility>
#include <set>
#include <map>

namespace rtso {

// 0 A.D. protocol constants
constexpr uint32_t PS_PROTOCOL_MAGIC          = 0x5073013f; // 'P', 's', 0x01, '?'
constexpr uint32_t PS_PROTOCOL_MAGIC_RESPONSE = 0x50630121; // 'P', 'c', 0x01, '!'
constexpr uint32_t PS_PROTOCOL_VERSION        = 0x01010019; // 0 A.D. protocol version
constexpr uint16_t PS_DEFAULT_PORT            = 20595;      // 0x5073 ('P', 's')

enum class NetRole : uint8_t {
	None = 0,
	Host = 1,
	Client = 2,
	Observer = 3
};

// Message types — mirrors 0 A.D. NetMessages.h NMT_*
enum class NetMessageType : uint8_t {
	NMT_INVALID                   = 0,
	NMT_SERVER_HANDSHAKE          = 1,
	NMT_CLIENT_HANDSHAKE          = 2,
	NMT_SERVER_HANDSHAKE_RESPONSE = 3,
	NMT_AUTHENTICATE              = 4,
	NMT_AUTHENTICATE_RESULT       = 5,
	NMT_CHAT                      = 6,
	NMT_READY                     = 7,
	NMT_CLEAR_ALL_READY           = 8,
	NMT_GAME_SETUP                = 9,
	NMT_ASSIGN_PLAYER             = 10,
	NMT_PLAYER_ASSIGNMENT         = 11,
	NMT_FILE_TRANSFER_REQUEST     = 12,
	NMT_FILE_TRANSFER_RESPONSE    = 13,
	NMT_FILE_TRANSFER_DATA        = 14,
	NMT_FILE_TRANSFER_ACK         = 15,
	NMT_JOIN_SYNC_START           = 16, // 0 A.D. join-sync marker; RTS0 uses it only
	                                // as a reserved value (the RTS0 late-join
	                                // resync is NMT_JOIN_SYNC_STATE below)
	NMT_REJOINED                  = 17,
	NMT_KICKED                    = 18,
	NMT_CLIENT_TIMEOUT            = 19,
	NMT_CLIENT_PERFORMANCE        = 20,
	NMT_CLIENTS_LOADING           = 21,
	NMT_CLIENT_PAUSED             = 22,
	NMT_LOADED_GAME               = 23,
	NMT_GAME_START                = 24,
	NMT_SAVED_GAME_START          = 25,
	NMT_END_COMMAND_BATCH         = 26,
	NMT_SYNC_CHECK                = 27,
	NMT_SYNC_ERROR                = 28,
	NMT_SIMULATION_COMMAND        = 29,
	NMT_FLARE                     = 30,
	// --- RTS0-local extensions (ids 1..30 mirror 0 A.D. NetMessages.h) ---
	// NMT_DIPLOMACY is retired: stances travel only as lockstep cmd 22, which
	// runs on the same turn on every peer and as the sender's seated player.
	// The id stays reserved and an inbound one is dropped. Do not reuse an
	// id >= NMT_DIPLOMACY for a different payload shape.
	NMT_DIPLOMACY                 = 31,
	// --- RTS0-local extensions, append-only (ids >= NMT_DIPLOMACY) -------------
	// NMT_JOIN_SYNC_STATE carries the late-join turn-0 state resync request and the
	// host's accept/reject reply (see JoinSyncMsg / TurnStateSnapshotMsg). It is
	// deliberately a NEW id rather than a reuse of 0 A.D.'s NMT_REJOINED (17) or
	// NMT_JOIN_SYNC_START (16) so old peers keep rejecting it as NMT_INVALID
	// instead of parsing it with the wrong payload shape.
	NMT_JOIN_SYNC_STATE           = 32,
	// Half-close of the late-join handshake: {accepted, hash}. Separate from
	// NMT_SYNC_CHECK so it cannot pollute the per-turn OOS accounting.
	NMT_JOIN_SYNC_CHECK           = 33,
	// Rejoin of a game in progress (0 A.D. NetServer OnJoinSyncingLoadedGame /
	// NMT_FILE_TRANSFER_* of the serialized state): see RejoinMsg.
	NMT_REJOIN_STATE              = 34,
	// A client's change to its own slot in game setup, "civ=3" / "team=2" /
	// "color=5" (0 A.D.'s NetServer lets only the controller send
	// NMT_GAME_SETUP; its TODO asks for player requests like this). The host
	// checks it and rebroadcasts NMT_GAME_SETUP, or answers that client
	// "refused=<why>". See NetSetupState.
	NMT_SETUP_REQUEST             = 35,
	NMT_COUNT                     = 36
};

enum AuthenticateResultCode : uint32_t {
	ARC_OK = 0,
	ARC_OK_SAVED_GAME = 1,
	ARC_OK_REJOINING = 2,
	ARC_PASSWORD_INVALID = 3,
	ARC_BANNED = 4 // 0 A.D. NDR_BANNED: the host banned this name or address
};

// NetCommand::cmd_type values. APPEND-ONLY: 1..23 are frozen wire values and
// must never be renumbered or reused for a different payload shape (they ride
// NMT_SIMULATION_COMMAND, the v2 replay layout and turn_history()). 24+ are
// RTS0-local and only valid on turn boundary paths.
//
//   1 Move          2 Stop         3 Attack      4 Gather      5 Build
//   6 Train         7 Stance       8 Formation   9 Research   10 Attack-move
//  11 Patrol       12 Garrison    13 Ungarrison 14 Rally      15 AttackBuilding
//  16 BuildRepair  17 Hunt        18 Board      19 Disembark  20 CancelTrain
//  21 Guard        22 Diplomacy   23 Barter
//
// NOTE cmd_type 22 (Diplomacy) is the ONLY route a stance takes into the sim:
// NMT_SIMULATION_COMMAND -> TurnManager::onMessage -> receive_remote_turn_batch
// -> TurnManager::tick applier, as the player the sending client is seated as.
// The stance matrix is mixed into compute_sim_hash(), so an out-of-band stance
// landing on a different turn on one peer is a guaranteed desync; that is why
// the old NMT_DIPLOMACY carrier was retired.
enum class NetCommandType : uint8_t {
	// 0 stays reserved as "unset", exactly as before this enum existed.
	NET_CMD_NONE          = 0,
	NET_CMD_MOVE          = 1,
	NET_CMD_STOP          = 2,
	NET_CMD_ATTACK        = 3,
	NET_CMD_GATHER        = 4,
	NET_CMD_BUILD         = 5,
	NET_CMD_TRAIN         = 6,
	NET_CMD_STANCE        = 7,
	NET_CMD_FORMATION     = 8,
	NET_CMD_RESEARCH      = 9,
	NET_CMD_ATTACK_MOVE   = 10,
	NET_CMD_PATROL        = 11,
	NET_CMD_GARRISON      = 12,
	NET_CMD_UNGARRISON    = 13,
	NET_CMD_RALLY         = 14, // param 1 = unset-rallypoint; NET_PARAM_QUEUED adds a position (Shift)
	NET_CMD_ATTACK_BUILD  = 15,
	NET_CMD_BUILD_REPAIR  = 16,
	NET_CMD_HUNT          = 17,
	NET_CMD_BOARD         = 18,
	NET_CMD_DISEMBARK     = 19,
	NET_CMD_CANCEL_TRAIN  = 20,
	NET_CMD_GUARD         = 21,
	NET_CMD_DIPLOMACY     = 22,
	NET_CMD_BARTER        = 23,
	// --- RTS0-local, appended after the frozen 1..23 block -------------------
	// Retired: Petra's fleet upkeep (navalManager.js maintainFleet) runs inside
	// the sim's naval manager now. The id stays reserved; nothing sends it.
	NET_CMD_MAINTAIN_FLEET = 24,
	// Train one civ roster unit by template (a hero, champion or mercenary that
	// the class-based NET_CMD_TRAIN can't name). target_id = building,
	// target_x = civ_unit_template_table() row (exact in a float), param = 0.
	NET_CMD_TRAIN_TEMPLATE = 25,
	// 0 A.D. Upgrade.js on a structure (sentry tower -> defense tower, wall ->
	// gate). target_id = building, param = 0 start / 1 cancel and refund.
	NET_CMD_UPGRADE_BUILDING = 26,
	// 0 A.D. "tribute" command (Player.js TributeResource): the issuing player
	// sends target_x (whole units, exact in a float) of resource `param`
	// (ResourceType) to player target_id. All or nothing; see Simulation::cmd_tribute.
	NET_CMD_TRIBUTE        = 27,
	// 0 A.D. Gate.js LockGate / UnlockGate. target_id = gate, param = 1 lock / 0 unlock.
	NET_CMD_LOCK_GATE      = 28,
	// 0 A.D. Pack / Unpack command (UnitAI Order.Pack / Order.Unpack) on the
	// selected siege engines. param = 1 pack / 0 unpack; see Simulation::order_pack.
	NET_CMD_PACK           = 29,
	// 0 A.D. "delete-entities": the issuing player's own units die and own structures
	// are destroyed (no loot). entity_ids = units and/or structures.
	NET_CMD_DELETE         = 30,
	// 0 A.D. "back-to-work" (UnitAI BackToWork) on the selected units.
	NET_CMD_BACK_TO_WORK   = 31,
	// 0 A.D. Heal order (UnitAI Heal): healers in entity_ids heal target_id (an injured
	// allied unit). param bit NET_PARAM_QUEUED queues it.
	NET_CMD_HEAL           = 32,
	// 0 A.D. "set-trading-goods" (Player.js SetTradingGoods): the issuing player's
	// trader goods shares in percent, one byte per ResourceType in target_id
	// (byte r = share of resource r); see Simulation::cmd_set_trading_goods.
	NET_CMD_SET_TRADING_GOODS = 33,
	// 0 A.D. "returnresource" (UnitAI ReturnResource): carriers in entity_ids drop their
	// load at dropsite target_id. param bit NET_PARAM_QUEUED queues it.
	NET_CMD_RETURN_RESOURCE = 34,
	// 0 A.D. "setup-trade-route" (Commands.js, UnitAI Trade): traders in entity_ids
	// trade between market target_id and the market whose id is target_x; `route` holds
	// the clicked waypoints between them as flat x,z pairs (UnitAI SetupTradeRoute route).
	// target_x 0 is a player's click with no source (Simulation::order_setup_trade): each
	// trader takes target_id as its first or second market; with NET_PARAM_CANCEL_TRADE it
	// is "cancel-setup-trade-route" on target_id. param bit NET_PARAM_QUEUED queues it.
	NET_CMD_SETUP_TRADE_ROUTE = 35,
	// 0 A.D. "alert-raise" / "alert-end" (AlertRaiser.js) on structure target_id;
	// param 0 raises, 1 ends.
	NET_CMD_ALERT = 36,
	// 0 A.D. "remove-guard" (UnitAI RemoveGuard) on the units in entity_ids.
	NET_CMD_REMOVE_GUARD = 37,
	// 0 A.D. "call-to-arms": the Soldiers, Warships, Siege and Healers in entity_ids drop
	// their load at the nearest dropsite, then attack-walk to (target_x, target_z).
	// param bit NET_PARAM_QUEUED queues it.
	NET_CMD_CALL_TO_ARMS = 38,
	// 39 is 0 A.D.'s "cheat" command (Cheat.js; turn_manager.cpp).
	// 0 A.D. "construct-wall" (Commands.js TryConstructWall, helpers/Walls.js): the issuing
	// player lays a WallSet wall from the point packed in target_id (Simulation::pack_wall_point)
	// to (target_x, target_z); entity_ids build it. param bit NET_PARAM_QUEUED queues it.
	NET_CMD_BUILD_WALL = 41,
	// 0 A.D. "upgrade" / "cancel-upgrade" on units (Upgrade.js): entity_ids upgrade to the
	// roster template in target_id (Simulation::cmd_upgrade_units); param 1 cancels.
	NET_CMD_UPGRADE_UNIT = 43,
	// 0 A.D. ProductionQueue.js EnableAutoQueue / DisableAutoQueue on structure target_id:
	// param 1 on, 0 off. (NET_CMD_TRAIN and NET_CMD_TRAIN_TEMPLATE carry their batch size
	// in target_z; 0, as older senders write, trains one.)
	NET_CMD_AUTOQUEUE = 44,
	// 0 A.D. "set-dropsite-sharing" on structure target_id: param 1 shares, 0 stops.
	NET_CMD_SET_DROPSITE_SHARING = 45,
	// 0 A.D. "dialog-answer" {"tutorial": "ready"}: the tutorial panel's Ready button
	// (maps/scripts/Tutorial.js). The sim only passes it to the map's triggers.
	NET_CMD_TUTORIAL_READY = 47,
	// 0 A.D. Trainer on a unit (triremes, Cyrus II): unit target_id trains the roster template
	// in target_x, batch size in target_z (param 0), cancels its queue item target_x (param 1), or
	// researches technology target_x (param 2; Researcher on a unit: Athenian triremes).
	NET_CMD_TRAIN_FROM_UNIT = 48,
	// 0 A.D. "resign" (Commands.js, the menu's Resign and ReturnQuestion's "I resign"):
	// the issuing player is defeated; see Simulation::cmd_resign. No fields.
	NET_CMD_RESIGN = 49,
	// 0 A.D. "focus-fire" (BuildingAI.AddFocusTarget): structures in entity_ids aim their arrows
	// at target_id first. param bit NET_PARAM_QUEUED adds it to the end; NET_PARAM_PUSH_FRONT puts it first.
	NET_CMD_FOCUS_FIRE = 50,
	// Gather on a supply the player remembers but which is gone (its mirage clicked): the units in
	// entity_ids gather the same kind near (target_x, target_z), else walk there. target_id = the
	// generic type | subtype << 8; param bit NET_PARAM_QUEUED as for gather (4).
	NET_CMD_GATHER_NEAR = 51,
	// Session control, not game commands (200+ is reserved for these; game command
	// ids keep growing from 44). Only the host sends them, inside its own turn batch,
	// so every client learns of a seat change before the turn it affects (0 A.D.
	// NetServerTurnManager::InitialiseClient / UninitialiseClient). The sim ignores them.
	// A client takes a seat: target_id = client id, param = player, target_x = the
	// first turn it sends a batch for.
	NET_CMD_PEER_JOINED    = 200,
	// A client left: target_id = client id, target_x = the last turn whose batch from
	// it the host holds; its batches after that are dropped everywhere.
	NET_CMD_PEER_LEFT      = 201,
};

// param bits on unit orders (move 1, attack 3, gather 4, attack-move 10, patrol 11,
// garrison 12, attack-building 15, build/repair 16, guard 21):
inline constexpr uint8_t NET_PARAM_QUEUED = 0x80;     // Shift: add to the order queue (0 A.D. "queued")
inline constexpr uint8_t NET_PARAM_UNITS_ONLY = 0x01; // attack-move: units only (targetClasses Unit)
inline constexpr uint8_t NET_PARAM_NO_CAPTURE = 0x02; // attack-move / attack-building: damage, don't capture (Ctrl)
inline constexpr uint8_t NET_PARAM_CAPTURE = 0x04;    // call-to-arms: capture (session.capture held)
inline constexpr uint8_t NET_PARAM_PUSH_FRONT = 0x04; // focus-fire: put the target first (0 A.D. "pushFront")
// session.pushorderfront on the unit orders above (1, 3, 4, 10, 11, 12, 15, 16, 21) and on
// NET_CMD_TRAIN_TEMPLATE: the order or batch goes first and the rest resumes after it
// (UnitAI PushOrderFront, ProductionQueue.AddItem pushFront). NET_CMD_RESEARCH says so
// with target_x 1.
inline constexpr uint8_t NET_PARAM_PUSH_FRONT_ORDER = 0x40;
// Build (5): param holds the building type, so the order flags ride above the packed angle
// (low 16 bits) of target_id (0 A.D. "construct" queued / pushFront).
inline constexpr uint32_t NET_BUILD_QUEUED = 1u << 16;
inline constexpr uint32_t NET_BUILD_PUSH_FRONT = 1u << 17;
inline bool net_cmd_takes_push_front(uint8_t cmd) {
	return cmd == 1 || cmd == 3 || cmd == 4 || cmd == 10 || cmd == 11 || cmd == 12 || cmd == 15 || cmd == 16 || cmd == 21;
}
inline constexpr uint8_t NET_PARAM_OCCUPY_TURRET = 0x01; // garrison: the holder's turret points (0 A.D. "occupy-turret")
inline constexpr uint8_t NET_PARAM_CANCEL_TRADE = 0x01;  // setup-trade-route: "cancel-setup-trade-route" on market target_id
inline constexpr uint8_t NET_HUNT_SCENERY_NO_CAPTURE = 3; // hunt (17) on scenery: attack it, don't capture it (Ctrl)

struct NetCommand {
	uint32_t client_id = 0;
	uint8_t cmd_type = 0; // NetCommandType as uint8; 1..23 frozen (see NetCommandType)
	std::vector<uint32_t> entity_ids;
	float target_x = 0.0f;
	float target_z = 0.0f;
	uint32_t target_id = 0;
	uint8_t param = 0;
	// P6 trade-route waypoints (0 A.D. setup-trade-route cmd.route): flat x,z pairs between
	// the two markets of NET_CMD_SETUP_TRADE_ROUTE (35). Empty on every other command.
	std::vector<float> route;
};

struct NetMessage {
	uint8_t type = 0;               // NetMessageType as uint8
	uint32_t turn = 0;              // turn index
	std::vector<uint8_t> payload;  // serialized payload
};

struct NetPeer {
	uint32_t id = 0;
	std::string address;
	uint16_t port = 0;
	bool connected = false;
	uint32_t rtt_ms = 0;        // smoothed RTT
	uint32_t packet_loss = 0;  // loss % (0-100)
	bool is_observer = false;  // observer vs active player
	std::string guid;
	std::string player_name;
	uint8_t ready_status = 0;      // host: 0.28 ReadyController 0 not ready, 1 ready, 2 stay ready
	uint32_t ready_version = 0;    // host: the setup version ready_status was given for
	bool is_authenticated = false; // host: admitted to the session (right password, not banned)
	uint64_t kick_disconnect_ms = 0; // host: kicked; the link closes at this time, after NMT_KICKED got there
	uint8_t disconnect_reason = 0;   // host: the NDR_* reason sent when that link closes (0 none)
	uint64_t last_seen_ms = 0;
	uint32_t last_sent_seq = 0;
	uint32_t last_recv_seq = 0;   // every reliable seq <= this has been delivered
	uint32_t last_ack_seq = 0;
	std::set<uint32_t> recv_ahead; // reliable seqs > last_recv_seq that arrived early, held in held_packets
	// ENet delivers a channel in order: an early reliable packet waits here, by
	// seq, until the ones before it arrive. {flags, payload}.
	std::map<uint32_t, std::pair<uint8_t, std::vector<uint8_t>>> held_packets;
	uint64_t held_since_ms = 0;      // when the oldest gap opened
	std::vector<uint8_t> fragments;  // a fragmented message's parts so far
};

// 0 A.D. NetServer.cpp MAX_CLIENTS: connected sessions a host takes (players
// and observers); one more is refused with NDR_SERVER_FULL.
constexpr size_t NET_MAX_CLIENTS = 41;
// Lockstep client ids the game setup lists, seats and counts for Ready (bit k
// of the 64-bit setup masks; the host is 0). Before Start a joiner takes the
// lowest free id, and at most NET_MAX_CLIENTS are connected, so every one fits.
constexpr uint32_t NET_SETUP_CLIENTS = 64;
static_assert(NET_MAX_CLIENTS <= NET_SETUP_CLIENTS, "every connected client needs a setup id");
// 0 A.D. NetHost.h ENetDisconnectReason values a host sends with its disconnect.
constexpr uint8_t NDR_INCORRECT_PROTOCOL_VERSION = 3;
// 0 A.D. NetMessages.h: the handshake response flag of a host that checks a
// joiner's name with the lobby.
constexpr uint32_t PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH = 0x1;
constexpr uint8_t NDR_SERVER_ALREADY_IN_GAME = 5; // a late observer the host does not take
constexpr uint8_t NDR_PLAYERNAME_IN_USE = 8;
constexpr uint8_t NDR_SERVER_FULL = 9;
constexpr uint8_t NDR_LOBBY_AUTH_FAILED = 10;
constexpr uint8_t NDR_INCORRECT_SOFTWARE_VERSION = 17;
// What this build sends in its handshake (0 A.D. CNetServerWorker::CheckHandshake
// compares engine versions and enabled mods): a host refuses any other. Raised whenever
// builds stop computing the same match from the same commands (0.2: det_math.h maths and
// the avalanched lockstep hash), so older builds are refused instead of falling out of sync.
constexpr const char *NET_SOFTWARE_VERSION = "RTS0-0.2";

struct NetConfig {
	uint16_t port = PS_DEFAULT_PORT; // 20595 (0x5073)
	uint8_t maxPlayers = 8;
	float snapshotRate = 20.0f; // Hz
	bool enableCompression = false;
	uint32_t snapshotChannel = 1; // unreliable channel
	uint32_t commandChannel = 0;  // reliable channel
};

struct NetStats {
	uint32_t rtt_ms = 0;
	uint32_t sent_bytes = 0;
	uint32_t recv_bytes = 0;
	uint32_t sent_packets = 0;
	uint32_t recv_packets = 0;
	uint32_t dropped_packets = 0;
};

// Message payloads for framing
struct ClientHandshakeMsg {
	uint32_t magic_response = PS_PROTOCOL_MAGIC_RESPONSE;
	uint32_t protocol_version = PS_PROTOCOL_VERSION;
	std::string engine_version = NET_SOFTWARE_VERSION;
	std::vector<std::pair<std::string, std::string>> enabled_mods;
};

struct ServerHandshakeMsg {
	uint32_t magic = PS_PROTOCOL_MAGIC;
	uint32_t protocol_version = PS_PROTOCOL_VERSION;
	std::string engine_version = NET_SOFTWARE_VERSION;
};

struct ServerHandshakeResponseMsg {
	uint32_t use_protocol_version = PS_PROTOCOL_VERSION;
	uint32_t flags = 0;
	std::string guid;
};

struct AuthenticateMsg {
	std::string name;
	std::string password;
	std::string controller_secret;
	// RTS0: the one-use ticket the lobby gave this joiner with the game's
	// address (/rooms/connect); the host redeems it to learn the joiner's
	// lobby name, as 0.28's host learns it from the lobby's LobbyAuth iq.
	// Optional on the wire.
	std::string lobby_ticket;
	std::string sent_name; // host: the name as sent, before SanitisePlayerName (not on the wire)
};

struct AuthenticateResultMsg {
	uint32_t code = ARC_OK;
	uint16_t host_id = 0;
	uint8_t is_controller = 0;
	std::string message;
};

struct ReadyMsg {
	std::string guid;
	uint8_t status = 1; // 0.28 ReadyController: 0 not ready, 1 ready, 2 stay ready (kept over setup changes)
	// The NetSetupState settings_version the client was ready for (RTS0;
	// optional on the wire). The host drops a ready for older settings, so
	// a ready sent just before a change cannot count for the new settings.
	uint32_t setup_version = 0;
};

struct GameStartMsg {
	std::string init_attributes;
};

// Lockstep seed-sync helpers (P1 playtest gate).
// Wire format stays a plain attributes string so old/new peers interop:
//   "map=<map_id>;seed=<u32>"  (extra keys ignored on parse)
// Full game setup rides as extra keys (0 A.D. NetServer game-start
// attributes broadcast before the first turn, NetMessages.h NMT_GAME_START):
//   victory=<0..6 or 128..255>;waves=<0|1>;lms=<0|1>;popcap=<0..1000>;ceasefire=<0..60 min>;mapsize=<128..512>;diff=<0..5>;behavior=<0..3>;res=<0..3>;
//   civ0=<civ>;civ1=<civ>;teams=<t0,t1,...> (+ up to 8 slots: civ2..civ7,
//   types=<ht,ht,...> with h=human a=ai x=closed, slots count = teams len)
// Old peers parse map+seed and ignore the rest.
// A Random civ in NMT_GAME_SETUP (the host picks one at Start, so
// NMT_GAME_START never carries it).
constexpr uint8_t RANDOM_CIV_WIRE = 255;
struct GameSetupParams {
	std::string map_id;
	uint32_t seed = 0;
	uint8_t victory = 0;    // rtso::VictoryCondition: 0 conquest, 1 wonder, 2 relic, 3 endless,
	                        // 4 conquest units, 5 conquest structures, 6 regicide; 0x80|mask a
	                        // combination (VICTORY_COMBINED, sim_types.h)
	uint8_t waves = 0;      // 1 = legacy wave-defense mode on
	uint8_t lms = 0;        // 1 = Last Man Standing (no allied victory)
	uint8_t saved_game = 0; // 1 = the host continues a saved game (0 A.D. NMT_SAVED_GAME_START):
	                        // joiners take the host's world by the rejoin transfer
	uint16_t pop_cap = 300;  // per-player population limit (0.28 default 300), 0 = unlimited
	uint16_t world_pop = 0;  // 0 A.D. World Population (shared by live players), 0 = off; wins over pop_cap
	uint16_t team_pop = 0;   // 0 A.D. 0.28 Team Population (per team, shared by its live players), 0 = off; wins over pop_cap
	uint8_t ceasefire = 0;  // minutes of ceasefire at match start
	uint8_t reveal = 0;     // 1 = revealed map (everything visible)
	uint8_t explore = 0;    // 1 = explored map (terrain known from the start)
	uint8_t ally = 0;       // 1 = Allied View (0 A.D. AllyView: SharedLosTech researched at start)
	uint8_t wonder_min = 20; // Wonder victory: minutes a wonder must stand, 0..60
	uint8_t relic_min = 20;  // Capture the Relic: minutes all relics must be held, 0..60
	uint8_t relics = 2;      // Capture the Relic: relic count, 1..16
	uint8_t biome = 0;       // random-map biome: 0 = the map's own, else BIOME_* + 1
	uint8_t diffs[8] = {3, 3, 3, 3, 3, 3, 3, 3}; // per-slot Petra difficulty 0..5
	uint8_t behs[8] = {};  // per-slot Petra behaviour 0..3 (0 random, player_defaults.json)
	uint8_t colors[8] = {0, 1, 2, 3, 4, 5, 6, 7}; // per-slot PLAYER_COLORS index 0..23 (one base-36 digit)
	uint8_t nomad = 0;             // 1 = Nomad start (random maps)
	uint8_t regicide_garrison = 0; // 1 = the Regicide hero may garrison
	uint8_t lock_teams = 0;        // 1 = 0 A.D. LockTeams: no diplomacy changes
	uint8_t cheats = 0;            // 1 = 0 A.D. CheatsEnabled
	uint8_t no_spies = 0;          // 1 = 0 A.D. DisableSpies
	uint8_t no_treasures = 0;      // 1 = 0 A.D. DisableTreasures
	std::string names[8];          // per-slot player names (0 A.D. PlayerName), empty = "Player N"
	uint8_t water_level = 0; // Flood: 0 rising, 1 shallow, 2 deep (0 A.D. WaterLevel)
	uint8_t team_placement = 0; // 0 A.D. TeamPlacement: rtso::TeamPlacement, 0 = the map picks, 5 = River
	uint8_t landscape = 0;      // 0 A.D. Landscape (Unknown map): RmsGenerator::UNKNOWN_LANDSCAPE_*, 0 = random
	uint8_t trigger_difficulty = 3; // 0 A.D. TriggerDifficulty: 1 Very Easy .. 5 Very Hard
	uint8_t daytime = 0;        // 0 A.D. Daytime: 0 = the map picks, else the map's option (1-based)
	uint8_t sea_rise_min = 25; // Extinct Volcano: minutes until the water rises, 0..60 (0 A.D. SeaLevelRiseTime)
	uint16_t map_size = 128; // random-map width in meters, 128..512
	uint8_t difficulty = 3; // Petra 0..5 sandbox..very hard
	uint8_t behavior = 0;   // Petra 0..3 random/def/bal/agg (player_defaults.json: random)
	uint8_t start_res = 1;  // 0 A.D. starting resources: 0 Very Low (100) .. 5 Deathmatch (50000), 1 Low (300)
	uint8_t civs[8] = {0, 1, 0, 0, 0, 0, 0, 0};
	char slot_types[8] = {'h', 'a', 'x', 'x', 'x', 'x', 'x', 'x'};
	uint8_t teams[8] = {1, 2, 0, 0, 0, 0, 0, 0};
	uint8_t slot_count = 2;
	// 0 A.D. gamesetup observers (a client assigned no player): bit k = lockstep
	// client k watches, takes no human slot and sends no orders.
	uint64_t observers = 0;
	// 0 A.D. gamesetup player assignment: seats[k] = the slot the host gave lockstep
	// client k, 0xFF = none chosen (the client takes the next free human slot).
	uint8_t seats[NET_SETUP_CLIENTS];
	GameSetupParams() { for (uint8_t &st : seats) st = 0xFF; }
	// 0 A.D. GameSpeed in hundredths (25..300): a networked match plays at
	// the speed set in game setup, on every peer.
	uint16_t speed = 100;
	// 1 = the map sets every slot's civ, team and colour (a scenario), so
	// clients may not ask to change theirs.
	uint8_t lock_players = 0;
	uint8_t rated = 0; // 1 = 0 A.D. RatingEnabled: the lobby rates the result
	// 0 A.D. -autostart-aiseed: the seed the AIs draw their personalities from, 0 = the
	// match seed (as every match before it had).
	uint32_t ai_seed = 0;
};
namespace detail {
// Player names ride in the attribute string, so the separators are %-escaped; at most
// MAX_NAME_BYTES bytes each, cut on a UTF-8 character boundary.
constexpr size_t MAX_NAME_BYTES = 48;
inline std::string escape_name(const std::string &n) {
	std::string in = n.substr(0, MAX_NAME_BYTES);
	if (n.size() > MAX_NAME_BYTES)
		while (!in.empty() && ((unsigned char)n[in.size()] & 0xC0) == 0x80) in.pop_back();
	std::string out;
	for (char c : in) {
		if (c == '%' || c == ';' || c == ',' || c == '=') {
			static const char kHex[] = "0123456789ABCDEF";
			out += '%'; out += kHex[(unsigned char)c >> 4]; out += kHex[(unsigned char)c & 15];
		} else if ((unsigned char)c >= 0x20) {
			out += c;
		}
	}
	return out;
}
inline std::string unescape_name(const std::string &v) {
	std::string out;
	auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
	for (size_t i = 0; i < v.size(); ++i) {
		if (v[i] == '%' && i + 2 < v.size() && hex(v[i + 1]) >= 0 && hex(v[i + 2]) >= 0) {
			out += (char)(hex(v[i + 1]) * 16 + hex(v[i + 2]));
			i += 2;
		} else if ((unsigned char)v[i] >= 0x20) {
			out += v[i];
		}
	}
	return out.substr(0, MAX_NAME_BYTES);
}
} // namespace detail
inline std::string format_game_start_full(const GameSetupParams &p) {
	std::string s = "map=" + p.map_id + ";seed=" + std::to_string(p.seed);
	s += ";victory=" + std::to_string((unsigned)p.victory);
	s += ";waves=" + std::to_string((unsigned)p.waves);
	s += ";lms=" + std::to_string((unsigned)p.lms);
	if (p.saved_game) s += ";saved=1";
	s += ";popcap=" + std::to_string((unsigned)p.pop_cap);
	if (p.world_pop) s += ";worldpop=" + std::to_string((unsigned)p.world_pop);
	if (p.team_pop) s += ";teampop=" + std::to_string((unsigned)p.team_pop);
	s += ";ceasefire=" + std::to_string((unsigned)p.ceasefire);
	s += ";reveal=" + std::to_string((unsigned)p.reveal);
	s += ";explore=" + std::to_string((unsigned)p.explore);
	if (p.ally) s += ";ally=1";
	s += ";wonderdur=" + std::to_string((unsigned)p.wonder_min);
	s += ";relicdur=" + std::to_string((unsigned)p.relic_min);
	s += ";relics=" + std::to_string((unsigned)p.relics);
	s += ";biome=" + std::to_string((unsigned)p.biome);
	{
		std::string d, b;
		for (unsigned i = 0; i < p.slot_count && i < 8; ++i) {
			if (i) { d += ','; b += ','; }
			d += (char)('0' + (p.diffs[i] <= 5 ? p.diffs[i] : 3));
			b += (char)('0' + (p.behs[i] <= 3 ? p.behs[i] : 2));
		}
		s += ";diffs=" + d + ";behs=" + b;
		std::string c;
		for (unsigned i = 0; i < p.slot_count && i < 8; ++i) {
			if (i) c += ',';
			c += "0123456789abcdefghijklmn"[p.colors[i] < 24 ? p.colors[i] : 0];
		}
		s += ";colors=" + c;
	}
	s += ";nomad=" + std::to_string((unsigned)p.nomad);
	s += ";rgarrison=" + std::to_string((unsigned)p.regicide_garrison);
	if (p.lock_teams) s += ";lockteams=1";
	if (p.cheats) s += ";cheats=1";
	if (p.no_spies) s += ";nospies=1";
	s += ";notreasures=" + std::to_string((unsigned)p.no_treasures);
	{
		std::string n;
		for (unsigned i = 0; i < p.slot_count && i < 8; ++i) {
			if (i) n += ',';
			n += detail::escape_name(p.names[i]);
		}
		s += ";names=" + n;
	}
	s += ";waterlevel=" + std::to_string((unsigned)p.water_level);
	if (p.team_placement) s += ";teamplace=" + std::to_string((unsigned)p.team_placement);
	if (p.landscape) s += ";landscape=" + std::to_string((unsigned)p.landscape);
	if (p.trigger_difficulty != 3) s += ";trigdiff=" + std::to_string((unsigned)p.trigger_difficulty);
	if (p.daytime) s += ";daytime=" + std::to_string((unsigned)p.daytime);
	s += ";searise=" + std::to_string((unsigned)p.sea_rise_min);
	s += ";mapsize=" + std::to_string((unsigned)p.map_size);
	s += ";diff=" + std::to_string((unsigned)p.difficulty);
	s += ";behavior=" + std::to_string((unsigned)p.behavior);
	s += ";res=" + std::to_string((unsigned)p.start_res);
	if (p.observers) s += ";obs=" + std::to_string((unsigned long long)p.observers);
	if (p.speed != 100) s += ";speed=" + std::to_string((unsigned)p.speed);
	if (p.lock_players) s += ";lockplayers=1";
	if (p.rated) s += ";rated=1";
	if (p.ai_seed) s += ";aiseed=" + std::to_string(p.ai_seed);
	{
		// One char per client id, at least 8 (older builds read exactly 8).
		std::string seats(8, '-');
		bool any = false;
		for (uint32_t k = 0; k < NET_SETUP_CLIENTS; ++k)
			if (p.seats[k] < 8) {
				if (seats.size() <= k) seats.resize(k + 1, '-');
				seats[k] = (char)('0' + p.seats[k]);
				any = true;
			}
		if (any) s += ";seats=" + seats;
	}
	char types[9] = {0}, teamstr[64] = {0};
	size_t tp = 0, sp = 0;
	for (unsigned i = 0; i < p.slot_count && i < 8; ++i) {
		if (tp < 8) types[tp++] = p.slot_types[i];
		s += ";civ" + std::to_string(i) + "=" + std::to_string((unsigned)p.civs[i]);
		if (sp < sizeof(teamstr) - 2) {
			if (i) teamstr[sp++] = ',';
			teamstr[sp++] = (char)('0' + (p.teams[i] < 10 ? p.teams[i] : 0));
		}
	}
	teamstr[sp] = 0;
	s += std::string(";types=") + types + ";teams=" + teamstr;
	return s;
}
namespace detail {
inline bool parse_u8(const std::string &v, uint8_t lo, uint8_t hi, uint8_t &out) {
	if (v.empty()) return false;
	unsigned val = 0;
	for (char c : v) {
		if (c < '0' || c > '9') return false;
		val = val * 10u + (unsigned)(c - '0');
	}
	if (val < lo || val > hi) return false;
	out = (uint8_t)val;
	return true;
}
// A 64-bit client mask in decimal.
inline bool parse_mask(const std::string &v, uint64_t &out) {
	if (v.empty() || v.size() > 20) return false;
	uint64_t val = 0;
	for (char c : v) {
		if (c < '0' || c > '9') return false;
		const uint64_t d = (uint64_t)(c - '0');
		if (val > (UINT64_MAX - d) / 10u) return false;
		val = val * 10u + d;
	}
	out = val;
	return true;
}
} // namespace detail
inline std::string format_game_start(const std::string &map_id, uint32_t seed) {
	GameSetupParams p;
	p.map_id = map_id;
	p.seed = seed;
	return format_game_start_full(p);
}
inline bool parse_game_start_full(const std::string &attrs, GameSetupParams &out) {
	out = GameSetupParams();
	bool have_map = false, have_seed = false;
	size_t pos = 0;
	while (pos <= attrs.size()) {
		size_t end = attrs.find(';', pos);
		if (end == std::string::npos) end = attrs.size();
		std::string kv = attrs.substr(pos, end - pos);
		size_t eq = kv.find('=');
		if (eq != std::string::npos) {
			std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
			if (k == "map") { out.map_id = v; have_map = true; }
			else if (k == "seed") {
				uint8_t dummy = 0;
				(void)dummy;
				if (v.empty()) return false;
				uint32_t val = 0;
				for (char c : v) {
					if (c < '0' || c > '9') return false;
					val = val * 10u + (uint32_t)(c - '0');
				}
				out.seed = val;
				have_seed = true;
			}
			else if (k == "aiseed") {
				uint64_t val = 0;
				bool ok = !v.empty() && v.size() <= 10;
				for (char c : v) {
					ok = ok && c >= '0' && c <= '9';
					val = val * 10u + (uint64_t)(c - '0');
				}
				if (ok && val <= 0xFFFFFFFFu) out.ai_seed = (uint32_t)val;
			}
			else if (k == "victory") { uint8_t x; if (detail::parse_u8(v, 0, 255, x) && (x <= 6 || x >= 0x80)) out.victory = x; }
			else if (k == "waves") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.waves = x; }
			else if (k == "lms") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.lms = x; }
			else if (k == "saved") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.saved_game = x; }
			else if (k == "ceasefire") { uint8_t x; if (detail::parse_u8(v, 0, 60, x)) out.ceasefire = x; }
			else if (k == "reveal") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.reveal = x; }
			else if (k == "explore") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.explore = x; }
			else if (k == "ally") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.ally = x; }
			else if (k == "wonderdur") { uint8_t x; if (detail::parse_u8(v, 0, 60, x)) out.wonder_min = x; }
			else if (k == "relicdur") { uint8_t x; if (detail::parse_u8(v, 0, 60, x)) out.relic_min = x; }
			else if (k == "relics") { uint8_t x; if (detail::parse_u8(v, 1, 16, x)) out.relics = x; }
			else if (k == "biome") { uint8_t x; if (detail::parse_u8(v, 0, 11, x)) out.biome = x; } // BIOME_COUNT (11: Nubia + 1)
			else if (k == "nomad") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.nomad = x; }
			else if (k == "names") {
				size_t a = 0;
				for (int slot = 0; slot < 8 && a <= v.size(); ++slot) {
					size_t b = v.find(',', a);
					if (b == std::string::npos) b = v.size();
					out.names[slot] = detail::unescape_name(v.substr(a, b - a));
					a = b + 1;
				}
			}
			else if (k == "notreasures") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.no_treasures = x; }
			else if (k == "rgarrison") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.regicide_garrison = x; }
			else if (k == "lockteams") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.lock_teams = x; }
			else if (k == "cheats") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.cheats = x; }
			else if (k == "nospies") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.no_spies = x; }
			else if (k == "colors") {
				for (size_t i = 0, slot = 0; i < v.size() && slot < 8; i += 2, ++slot) {
					const char c = v[i];
					if (c >= '0' && c <= '9') out.colors[slot] = (uint8_t)(c - '0');
					else if (c >= 'a' && c <= 'n') out.colors[slot] = (uint8_t)(c - 'a' + 10);
				}
			}
			else if (k == "diffs" || k == "behs") {
				const bool diffs = k == "diffs";
				for (size_t i = 0, slot = 0; i < v.size() && slot < 8; i += 2, ++slot) {
					const char c = v[i];
					if (c >= '0' && c <= (diffs ? '5' : '3')) (diffs ? out.diffs : out.behs)[slot] = (uint8_t)(c - '0');
				}
			}
			else if (k == "waterlevel") { uint8_t x; if (detail::parse_u8(v, 0, 2, x)) out.water_level = x; }
			else if (k == "teamplace") { uint8_t x; if (detail::parse_u8(v, 0, 5, x)) out.team_placement = x; }
			else if (k == "landscape") { uint8_t x; if (detail::parse_u8(v, 0, 15, x)) out.landscape = x; }
			else if (k == "trigdiff") { uint8_t x; if (detail::parse_u8(v, 1, 5, x)) out.trigger_difficulty = x; }
			else if (k == "daytime") { uint8_t x; if (detail::parse_u8(v, 0, 2, x)) out.daytime = x; }
			else if (k == "searise") { uint8_t x; if (detail::parse_u8(v, 0, 60, x)) out.sea_rise_min = x; }
			else if (k == "popcap" && !v.empty() && v.size() <= 4 && v.find_first_not_of("0123456789") == std::string::npos) {
				unsigned x = (unsigned)std::stoul(v);
				if (x <= 1000) out.pop_cap = (uint16_t)x;
			}
			else if (k == "worldpop" && !v.empty() && v.size() <= 5 && v.find_first_not_of("0123456789") == std::string::npos) {
				unsigned x = (unsigned)std::stoul(v);
				if (x <= 63000) out.world_pop = (uint16_t)x;
			}
			else if (k == "teampop" && !v.empty() && v.size() <= 5 && v.find_first_not_of("0123456789") == std::string::npos) {
				unsigned x = (unsigned)std::stoul(v);
				if (x <= 10000) out.team_pop = (uint16_t)x;
			}
			else if (k == "mapsize" && !v.empty() && v.size() <= 3 && v.find_first_not_of("0123456789") == std::string::npos) {
				unsigned x = (unsigned)std::stoul(v);
				if (x >= 128 && x <= 512) out.map_size = (uint16_t)x;
			}
			else if (k == "diff") { uint8_t x; if (detail::parse_u8(v, 0, 5, x)) out.difficulty = x; }
			else if (k == "behavior") { uint8_t x; if (detail::parse_u8(v, 0, 3, x)) out.behavior = x; }
			else if (k == "res") { uint8_t x; if (detail::parse_u8(v, 0, 5, x)) out.start_res = x; }
			else if (k == "obs") { uint64_t x; if (detail::parse_mask(v, x)) out.observers = x; }
			else if (k == "speed" && !v.empty() && v.size() <= 4 && v.find_first_not_of("0123456789") == std::string::npos) {
				unsigned x = (unsigned)std::stoul(v);
				if (x >= 10 && x <= 2000) out.speed = (uint16_t)x; // game_speeds.json: Turtle 0.1x .. 20x
			}
			else if (k == "lockplayers") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.lock_players = x; }
			else if (k == "rated") { uint8_t x; if (detail::parse_u8(v, 0, 1, x)) out.rated = x; }
			else if (k == "seats" && v.size() >= 8 && v.size() <= NET_SETUP_CLIENTS) {
				for (size_t c = 0; c < v.size(); ++c) out.seats[c] = (v[c] >= '0' && v[c] <= '7') ? (uint8_t)(v[c] - '0') : 0xFF;
			}
			else if (k.size() == 4 && k[0] == 'c' && k[1] == 'i' && k[2] == 'v' && k[3] >= '0' && k[3] <= '7') {
				// Any civ id (Germans is 15); callers drop Gaia with is_playable_civ.
				uint8_t x; if (detail::parse_u8(v, 0, 255, x) && (x <= 31 || x == RANDOM_CIV_WIRE)) out.civs[k[3] - '0'] = x;
			}
			else if (k == "types") {
				for (size_t i = 0; i < v.size() && i < 8; ++i)
					if (v[i] == 'h' || v[i] == 'a' || v[i] == 'x') out.slot_types[i] = v[i];
				if (!v.empty()) out.slot_count = (uint8_t)(v.size() < 8 ? v.size() : 8);
			}
			else if (k == "teams") {
				size_t i = 0;
				size_t p2 = 0;
				while (i < 8) {
					size_t c = v.find(',', p2);
					std::string tok = (c == std::string::npos) ? v.substr(p2) : v.substr(p2, c - p2);
					uint8_t x;
					if (!detail::parse_u8(tok, 0, 8, x)) break;
					out.teams[i++] = x;
					if (c == std::string::npos) break;
					p2 = c + 1;
				}
			}
		}
		if (end == attrs.size()) break;
		pos = end + 1;
	}
	return have_map && have_seed;
}
// NMT_GAME_SETUP (0 A.D. NetServer OnGameSetup / NetClient OnGameSetup): the
// host's game setup before Start, sent to every client whenever it changes:
// the settings as NMT_GAME_START attributes plus who sits where and who is
// ready (0 A.D.'s NMT_PLAYER_ASSIGNMENT statuses). Extra keys, which
// parse_game_start_full ignores:
//   nsv=<settings version> nsq=<broadcast sequence> nsc=<8 comma-separated:
//   slot i's client id, '-' none> ncl=<connected clients mask> nsr=<ready mask>
//   nss=<stay ready mask> nso=<observers mask> nsn=<client names, by client id>
// The masks are 64-bit (NET_SETUP_CLIENTS): a full 4v4 with observers lists all.
struct NetSetupState {
	GameSetupParams setup;
	uint32_t settings_version = 0; // bumps when a setting or a seat changes
	uint32_t seq = 0;              // bumps on every broadcast; clients keep the newest
	int8_t slot_client[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	uint64_t clients = 0;   // bit k: client k is in the game (the host is client 0)
	uint64_t ready = 0;     // bit k: client k is ready for these settings (the host never needs to be)
	uint64_t stay_ready = 0; // bit k: client k stays ready whatever changes
	uint64_t observers = 0; // bit k: client k watches
	std::string names[NET_SETUP_CLIENTS];  // by client id
	// The slot client k plays, -1 if none.
	int slot_of(uint32_t client) const {
		for (int i = 0; i < 8; ++i)
			if (slot_client[i] == (int)client) return i;
		return -1;
	}
	// 0 A.D. gamesetup start button: every client that plays is ready.
	bool all_ready() const {
		for (uint32_t i = 1; i < NET_SETUP_CLIENTS; ++i)
			if (((clients >> i) & 1) && slot_of(i) >= 0 && !(((ready | stay_ready) >> i) & 1)) return false;
		return true;
	}
};
inline std::string format_net_setup(const NetSetupState &n) {
	std::string s = format_game_start_full(n.setup);
	std::string seats;
	for (int i = 0; i < 8; ++i)
		seats += std::string(i ? "," : "") + (n.slot_client[i] >= 0 && (uint32_t)n.slot_client[i] < NET_SETUP_CLIENTS
				? std::to_string(n.slot_client[i]) : std::string("-"));
	s += ";nsv=" + std::to_string(n.settings_version) + ";nsq=" + std::to_string(n.seq) + ";nsc=" + seats;
	s += ";ncl=" + std::to_string((unsigned long long)n.clients) + ";nsr=" + std::to_string((unsigned long long)n.ready) +
	     ";nss=" + std::to_string((unsigned long long)n.stay_ready) +
	     ";nso=" + std::to_string((unsigned long long)n.observers) + ";nsn=";
	uint32_t count = 8; // names up to the highest client in the game
	for (uint32_t i = 8; i < NET_SETUP_CLIENTS; ++i)
		if ((n.clients >> i) & 1) count = i + 1;
	for (uint32_t i = 0; i < count; ++i) s += (i ? "," : "") + detail::escape_name(n.names[i]);
	return s;
}
inline bool parse_net_setup(const std::string &attrs, NetSetupState &out) {
	out = NetSetupState();
	if (!parse_game_start_full(attrs, out.setup)) return false;
	bool have_version = false;
	size_t pos = 0;
	while (pos <= attrs.size()) {
		size_t end = attrs.find(';', pos);
		if (end == std::string::npos) end = attrs.size();
		const std::string kv = attrs.substr(pos, end - pos);
		const size_t eq = kv.find('=');
		if (eq != std::string::npos) {
			const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
			const bool num = !v.empty() && v.size() <= 10 && v.find_first_not_of("0123456789") == std::string::npos;
			if ((k == "nsv" || k == "nsq") && num) {
				const unsigned long x = std::stoul(v);
				if (x <= 0xFFFFFFFFul) {
					(k == "nsv" ? out.settings_version : out.seq) = (uint32_t)x;
					have_version |= k == "nsv";
				}
			} else if (k == "nsc") {
				size_t a = 0;
				for (int i = 0; i < 8 && a <= v.size(); ++i) {
					size_t b = v.find(',', a);
					if (b == std::string::npos) b = v.size();
					uint8_t c;
					out.slot_client[i] = detail::parse_u8(v.substr(a, b - a), 0, NET_SETUP_CLIENTS - 1, c) ? (int8_t)c : -1;
					a = b + 1;
				}
			} else if (k == "ncl" || k == "nsr" || k == "nss" || k == "nso") {
				uint64_t x;
				if (detail::parse_mask(v, x))
					(k == "ncl" ? out.clients : k == "nsr" ? out.ready : k == "nss" ? out.stay_ready : out.observers) = x;
			} else if (k == "nsn") {
				size_t a = 0;
				for (uint32_t c = 0; c < NET_SETUP_CLIENTS && a <= v.size(); ++c) {
					size_t b = v.find(',', a);
					if (b == std::string::npos) b = v.size();
					out.names[c] = detail::unescape_name(v.substr(a, b - a));
					a = b + 1;
				}
			}
		}
		if (end == attrs.size()) break;
		pos = end + 1;
	}
	return have_version;
}
inline bool parse_game_start_params(const std::string &attrs, std::string &map_id_out, uint32_t &seed_out) {
	GameSetupParams p;
	if (!parse_game_start_full(attrs, p)) return false;
	map_id_out = p.map_id;
	seed_out = p.seed;
	return true;
}

struct SimulationCommandMsg {
	uint32_t turn = 0;
	uint32_t client_id = 0;
	std::vector<NetCommand> commands;
};

struct EndCommandBatchMsg {
	uint32_t turn = 0;
	uint16_t turn_length = 200; // ms
};

struct SyncCheckMsg {
	uint32_t turn = 0;
	uint32_t hash = 0;
	std::string hash_str;
};

struct SyncErrorMsg {
	uint32_t turn = 0;
	uint32_t hash_expected = 0;
	std::vector<std::string> player_names;
};

struct ChatMsg {
	std::string sender_guid;            // stamped by the host from the session (0 A.D. NetServer OnChat)
	std::string message;
	std::vector<std::string> receivers; // empty = broadcast to all; "peer-<id>" GUIDs otherwise
};
inline std::string peer_guid(uint32_t peer_id) { return "peer-" + std::to_string(peer_id); }

// NMT_REJOIN_STATE: a client (re)joining a match in progress takes over a
// human seat whose client dropped. It asks the host (REQUEST); at its next turn
// boundary T the host answers with a HEADER (turn T, the seat, the client ->
// player map, the lockstep peers and the turn each must send from, and every
// command batch it holds for turns after T) and then the saved world in CHUNKs.
// The rejoiner loads the world, takes the batches and runs on from turn T+1.
struct RejoinMsg {
	// LOADED: a rejoining player has loaded the world (0 A.D. NMT_LOADED_GAME);
	// LOCKSTEP: the host's answer, turn = the first turn the lockstep waits on it.
	enum Kind : uint8_t { REQUEST = 0, REJECT = 1, HEADER = 2, CHUNK = 3, LOADED = 4, LOCKSTEP = 5 };
	uint8_t kind = REQUEST;
	uint32_t turn = 0;
	uint32_t total = 0;  // HEADER: saved-world size; CHUNK: offset of these bytes
	uint8_t reason = 0;  // REJECT: 1 = no free seat, 2 = match not started, 6 = no late observers,
	                     //   7 = observer limit reached (0 A.D. NDR_SERVER_FULL);
	                     // REQUEST: 1 = join as an observer (0 A.D. observer late join)
	std::vector<uint8_t> bytes; // HEADER: the rest of the header; CHUNK: world bytes
	static constexpr size_t CHUNK_BYTES = 1024;
};

// NMT_CLIENT_PAUSED (0 A.D. NetMessages.h ClientPaused): a client pauses or
// resumes the match. The host stamps guid with the sender's id ("<client id>"),
// keeps the list of pausing clients and relays the message to everyone else.
struct ClientPausedMsg {
	std::string guid;
	uint8_t pause = 0;
};

// NMT_CLIENT_PERFORMANCE payload (0 A.D. CClientPerformanceMessage, with the
// lastReceivedTime of CClientTimeoutMessage folded in): once a second the host
// tells every client how each connected client is doing. rtt_ms is the host's
// smoothed round trip to it; silent_ms how long since the host last heard it.
// NMT_KICKED payload (0 A.D. CKickedMessage): the host removed a client, by
// guid ("peer-<id>") and name; ban = it may not come back.
struct KickedMsg {
	std::string guid;
	std::string name;
	uint8_t ban = 0;
};

struct ClientPerformanceMsg {
	struct Entry {
		uint32_t client = 0;
		uint32_t rtt_ms = 0;
		uint32_t silent_ms = 0;
	};
	std::vector<Entry> clients;
};
// NMT_PLAYER_ASSIGNMENT (0 A.D. CPlayerAssignmentMessage): the host's list of the
// match's clients, sent whenever it changes: each client's id, name, the player
// it plays (0xFF for an observer) and whether it plays, observes or has left.
struct PlayerAssignmentMsg {
	enum Status : uint8_t { PLAYING = 0, OBSERVER = 1, LEFT = 2 };
	struct Entry {
		uint32_t client = 0;
		std::string name;
		uint8_t player = 0xFF;
		uint8_t status = PLAYING;
		bool operator==(const Entry &o) const {
			return client == o.client && name == o.name && player == o.player && status == o.status;
		}
	};
	std::vector<Entry> clients;
};

// 0 A.D. session.js: a round trip above this reads as a bad connection, and a
// client unheard for longer than this is "not responding".
inline constexpr uint32_t NET_LAG_WARN_RTT_MS = 500;
inline constexpr uint32_t NET_TIMEOUT_WARN_MS = 1000;
// 0 A.D.'s network warnings overlay (overlay.netwarnings): NetSession.h
// NETWORK_WARNING_TIMEOUT, NetClient.cpp NETWORK_BAD_PING (DEFAULT_TURN_LENGTH
// 200 ms x COMMAND_DELAY_MP 4 / 2) and network.js g_NetworkWarningTimeout.
inline constexpr uint32_t NETWORK_WARNING_TIMEOUT_MS = 2000;
inline constexpr uint32_t NETWORK_BAD_PING_MS = 400;
inline constexpr uint32_t NETWORK_WARNING_SHOW_MS = 3000;

// NMT_JOIN_SYNC_STATE payload -- late-join turn-0 state resync request.
// 0 A.D. parity: NetServer.cpp:1366 OnGameStart re-sends the game setup to a
// session that arrives after the first broadcast, and NetClient.cpp:826 OnGameStart
// is a legal transition in state NCS_JOIN_SYNCING (NetClient.cpp:108). In RTS0 one
// message type carries BOTH halves of that handshake -- an enum id is append-only,
// but a single id may carry any payload as long as two ids never collide:
//   * ROLE_JOINER -> the invitee is asking for setup + turn-0 state.
//   * ROLE_HOST   -> the host answers with NMT_GAME_START(full setup) followed by
//                   NMT_JOIN_SYNC_START(ROLE_JOINER, requested_turn).
//
// The sync is REQUESTED, never pushed: a peer must not reset its world because a
// stranger dialed in. The host answers only to an admitted peer, so the resend is
// a pure replay of the attributes it is already broadcasting to everyone.
struct JoinSyncMsg {
	enum Role : uint8_t {
		ROLE_JOINER   = 0, // "send me turn-N state"
		ROLE_HOST     = 1, // "here is the turn-N sync state"
	};
	uint8_t role = ROLE_JOINER;
	uint32_t requested_turn = 0; // JOINER: the turn it wants to resume at
};

// Turn-0 (or turn-N) state request, hosted by NMT_JOIN_SYNC_STATE. Asserting that
// the invariant below holds in the two processes that matter is what keeps this
// honest: a late joiner's boot world is built from the same map+seed, so a
// mismatch here is a hard desync and MUST abort the join rather than continue.
struct TurnStateSnapshotMsg {
	uint32_t turn = 0; // turn the snapshot describes
	uint32_t hash = 0; // TurnManager::compute_sim_hash at that turn
	bool accepted = 0; // 1 = host will resend setup, 0 = rejected (see error)
	uint8_t error = 0; // Error code; meaningful only when accepted == 0
	enum Error : uint8_t {
		ERR_UNKNOWN           = 0,
		ERR_PEER_ADMITTED     = 1, // requester is not an admitted peer
		ERR_NOT_AUTHORITATIVE = 2, // host has no GAME_START applied yet
		ERR_JOINED_LATE       = 3, // host already past the accepted window
	};
};

// Late-join turn-0 state request path (P3 GAME_START resend):
//
//   joiner -> NMT_JOIN_SYNC_STATE{ROLE_JOINER, requested_turn}   -> host
//   host   -> NMT_JOIN_SYNC_STATE{ROLE_HOST, turn}              (accepted)
//   host   -> NMT_GAME_START{full setup}                       (resend)
//   host   -> NMT_JOIN_SYNC_STATE{ROLE_JOINER, turn}            (turn-0 request)
//   joiner -> NMT_JOIN_SYNC_CHECK{accepted, hash}               -> host
//   joiner -> NMT_JOIN_SYNC_CHECK{accepted=0, hash=0}           -> host when the
//                                                                    rebuilt
//                                                                    turn-0 hash
//                                                                    mismatches
//
// NMT_JOIN_SYNC_CHECK (id 33) is the ack half of this path. Both sides use the
// same frame: {role = ROLE_JOINER, turn, accepted, hash, error}.
// Do NOT fold the snapshot into NMT_SYNC_CHECK: that message is broadcast every
// lockstep turn, is consumed by receive_sync_check() -> has_out_of_sync(), and
// folding a handshake into it would need an indistinguishable special case and
// would mix the handshake into the per-turn OOS accounting.
struct JoinSyncCheckMsg {
	uint8_t role = JoinSyncMsg::ROLE_JOINER;
	uint8_t accepted = 0;
	uint8_t error = 0;
	uint32_t turn = 0;
	uint32_t hash = 0;
};

// Binary serialization utilities
namespace NetSerde {

inline void write_u8(std::vector<uint8_t> &buf, uint8_t val) {
	buf.push_back(val);
}

inline void write_u16(std::vector<uint8_t> &buf, uint16_t val) {
	buf.push_back(static_cast<uint8_t>(val & 0xFF));
	buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

inline void write_u32(std::vector<uint8_t> &buf, uint32_t val) {
	buf.push_back(static_cast<uint8_t>(val & 0xFF));
	buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
	buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
	buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

inline void write_float(std::vector<uint8_t> &buf, float val) {
	uint32_t bits = 0;
	std::memcpy(&bits, &val, sizeof(float));
	write_u32(buf, bits);
}

inline void write_string(std::vector<uint8_t> &buf, const std::string &str) {
	write_u16(buf, static_cast<uint16_t>(str.size()));
	buf.insert(buf.end(), str.begin(), str.end());
}

inline bool read_u8(const uint8_t *data, size_t size, size_t &offset, uint8_t &val) {
	if (offset + 1 > size) return false;
	val = data[offset++];
	return true;
}

inline bool read_u16(const uint8_t *data, size_t size, size_t &offset, uint16_t &val) {
	if (offset + 2 > size) return false;
	val = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
	offset += 2;
	return true;
}

inline bool read_u32(const uint8_t *data, size_t size, size_t &offset, uint32_t &val) {
	if (offset + 4 > size) return false;
	val = static_cast<uint32_t>(data[offset]) |
	      (static_cast<uint32_t>(data[offset + 1]) << 8) |
	      (static_cast<uint32_t>(data[offset + 2]) << 16) |
	      (static_cast<uint32_t>(data[offset + 3]) << 24);
	offset += 4;
	return true;
}

inline bool read_float(const uint8_t *data, size_t size, size_t &offset, float &val) {
	uint32_t bits = 0;
	if (!read_u32(data, size, offset, bits)) return false;
	std::memcpy(&val, &bits, sizeof(float));
	return true;
}

inline bool read_string(const uint8_t *data, size_t size, size_t &offset, std::string &str) {
	uint16_t len = 0;
	if (!read_u16(data, size, offset, len)) return false;
	if (offset + len > size) return false;
	str.assign(reinterpret_cast<const char*>(data + offset), len);
	offset += len;
	return true;
}

inline void serialize_net_command(std::vector<uint8_t> &buf, const NetCommand &cmd) {
	write_u32(buf, cmd.client_id);
	write_u8(buf, cmd.cmd_type);
	write_float(buf, cmd.target_x);
	write_float(buf, cmd.target_z);
	write_u32(buf, cmd.target_id);
	write_u8(buf, cmd.param);
	write_u32(buf, static_cast<uint32_t>(cmd.entity_ids.size()));
	for (uint32_t id : cmd.entity_ids) {
		write_u32(buf, id);
	}
	// The trade route rides at the end, so commands from older builds (which end at
	// entity_ids) still read: a missing tail is an empty route.
	write_u32(buf, static_cast<uint32_t>(cmd.route.size()));
	for (float v : cmd.route) {
		write_float(buf, v);
	}
}

inline bool deserialize_net_command(const uint8_t *data, size_t size, size_t &offset, NetCommand &cmd) {
	if (!read_u32(data, size, offset, cmd.client_id)) return false;
	if (!read_u8(data, size, offset, cmd.cmd_type)) return false;
	if (!read_float(data, size, offset, cmd.target_x)) return false;
	if (!read_float(data, size, offset, cmd.target_z)) return false;
	if (!read_u32(data, size, offset, cmd.target_id)) return false;
	if (!read_u8(data, size, offset, cmd.param)) return false;
	uint32_t count = 0;
	if (!read_u32(data, size, offset, count)) return false;
	cmd.entity_ids.clear();
	cmd.entity_ids.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t id = 0;
		if (!read_u32(data, size, offset, id)) return false;
		cmd.entity_ids.push_back(id);
	}
	cmd.route.clear();
	if (offset == size) return true; // an older build's command: no route
	uint32_t nroute = 0;
	if (!read_u32(data, size, offset, nroute)) return false;
	if (nroute > 2 * 64) return false; // longer than any route order_trade keeps
	cmd.route.reserve(nroute);
	for (uint32_t i = 0; i < nroute; ++i) {
		float v = 0.0f;
		if (!read_float(data, size, offset, v)) return false;
		cmd.route.push_back(v);
	}
	return true;
}

} // namespace NetSerde

class NetPacketSerializer {
public:
	static std::vector<uint8_t> frame_client_handshake(const ClientHandshakeMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.magic_response);
		NetSerde::write_u32(buf, msg.protocol_version);
		NetSerde::write_string(buf, msg.engine_version);
		NetSerde::write_u16(buf, static_cast<uint16_t>(msg.enabled_mods.size()));
		for (const auto &m : msg.enabled_mods) {
			NetSerde::write_string(buf, m.first);
			NetSerde::write_string(buf, m.second);
		}
		return buf;
	}

	static bool parse_client_handshake(const uint8_t *data, size_t size, ClientHandshakeMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.magic_response)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.protocol_version)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.engine_version)) return false;
		uint16_t mod_count = 0;
		if (!NetSerde::read_u16(data, size, offset, mod_count)) return false;
		msg.enabled_mods.clear();
		for (uint16_t i = 0; i < mod_count; ++i) {
			std::string name, ver;
			if (!NetSerde::read_string(data, size, offset, name)) return false;
			if (!NetSerde::read_string(data, size, offset, ver)) return false;
			msg.enabled_mods.emplace_back(name, ver);
		}
		return true;
	}

	static std::vector<uint8_t> frame_server_handshake(const ServerHandshakeMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.magic);
		NetSerde::write_u32(buf, msg.protocol_version);
		NetSerde::write_string(buf, msg.engine_version);
		return buf;
	}

	static bool parse_server_handshake(const uint8_t *data, size_t size, ServerHandshakeMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.magic)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.protocol_version)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.engine_version)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_server_handshake_response(const ServerHandshakeResponseMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.use_protocol_version);
		NetSerde::write_u32(buf, msg.flags);
		NetSerde::write_string(buf, msg.guid);
		return buf;
	}

	static bool parse_server_handshake_response(const uint8_t *data, size_t size, ServerHandshakeResponseMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.use_protocol_version)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.flags)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.guid)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_authenticate(const AuthenticateMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.name);
		NetSerde::write_string(buf, msg.password);
		NetSerde::write_string(buf, msg.controller_secret);
		if (!msg.lobby_ticket.empty()) NetSerde::write_string(buf, msg.lobby_ticket);
		return buf;
	}

	static bool parse_authenticate(const uint8_t *data, size_t size, AuthenticateMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.name)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.password)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.controller_secret)) return false;
		if (offset < size && !NetSerde::read_string(data, size, offset, msg.lobby_ticket)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_authenticate_result(const AuthenticateResultMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.code);
		NetSerde::write_u16(buf, msg.host_id);
		NetSerde::write_u8(buf, msg.is_controller);
		NetSerde::write_string(buf, msg.message);
		return buf;
	}

	static bool parse_authenticate_result(const uint8_t *data, size_t size, AuthenticateResultMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.code)) return false;
		if (!NetSerde::read_u16(data, size, offset, msg.host_id)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.is_controller)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.message)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_ready(const ReadyMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.guid);
		NetSerde::write_u8(buf, msg.status);
		NetSerde::write_u32(buf, msg.setup_version);
		return buf;
	}

	static bool parse_ready(const uint8_t *data, size_t size, ReadyMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.guid)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.status)) return false;
		msg.setup_version = 0;
		if (offset < size && !NetSerde::read_u32(data, size, offset, msg.setup_version)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_game_start(const GameStartMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.init_attributes);
		return buf;
	}

	static bool parse_game_start(const uint8_t *data, size_t size, GameStartMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.init_attributes)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_simulation_command(const SimulationCommandMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u32(buf, msg.client_id);
		NetSerde::write_u32(buf, static_cast<uint32_t>(msg.commands.size()));
		for (const auto &c : msg.commands) {
			NetSerde::serialize_net_command(buf, c);
		}
		return buf;
	}

	static bool parse_simulation_command(const uint8_t *data, size_t size, SimulationCommandMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.client_id)) return false;
		uint32_t count = 0;
		if (!NetSerde::read_u32(data, size, offset, count)) return false;
		msg.commands.clear();
		msg.commands.reserve(count);
		for (uint32_t i = 0; i < count; ++i) {
			NetCommand c;
			if (!NetSerde::deserialize_net_command(data, size, offset, c)) return false;
			msg.commands.push_back(std::move(c));
		}
		return true;
	}

	static std::vector<uint8_t> frame_end_command_batch(const EndCommandBatchMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u16(buf, msg.turn_length);
		return buf;
	}

	static bool parse_end_command_batch(const uint8_t *data, size_t size, EndCommandBatchMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u16(data, size, offset, msg.turn_length)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_sync_check(const SyncCheckMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u32(buf, msg.hash);
		NetSerde::write_string(buf, msg.hash_str.empty() ? std::to_string(msg.hash) : msg.hash_str);
		return buf;
	}

	static bool parse_sync_check(const uint8_t *data, size_t size, SyncCheckMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.hash)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.hash_str)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_sync_error(const SyncErrorMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u32(buf, msg.hash_expected);
		NetSerde::write_u16(buf, static_cast<uint16_t>(msg.player_names.size()));
		for (const auto &name : msg.player_names) {
			NetSerde::write_string(buf, name);
		}
		return buf;
	}

	static bool parse_sync_error(const uint8_t *data, size_t size, SyncErrorMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.hash_expected)) return false;
		uint16_t count = 0;
		if (!NetSerde::read_u16(data, size, offset, count)) return false;
		msg.player_names.clear();
		for (uint16_t i = 0; i < count; ++i) {
			std::string name;
			if (!NetSerde::read_string(data, size, offset, name)) return false;
			msg.player_names.push_back(std::move(name));
		}
		return true;
	}

	static std::vector<uint8_t> frame_chat(const ChatMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.sender_guid);
		NetSerde::write_string(buf, msg.message);
		NetSerde::write_u16(buf, static_cast<uint16_t>(msg.receivers.size()));
		for (const auto &r : msg.receivers) {
			NetSerde::write_string(buf, r);
		}
		return buf;
	}

	static bool parse_chat(const uint8_t *data, size_t size, ChatMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.sender_guid)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.message)) return false;
		uint16_t count = 0;
		if (!NetSerde::read_u16(data, size, offset, count)) return false;
		msg.receivers.clear();
		for (uint16_t i = 0; i < count; ++i) {
			std::string r;
			if (!NetSerde::read_string(data, size, offset, r)) return false;
			msg.receivers.push_back(std::move(r));
		}
		return true;
	}

	static std::vector<uint8_t> frame_rejoin(const RejoinMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u8(buf, msg.kind);
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u32(buf, msg.total);
		NetSerde::write_u8(buf, msg.reason);
		buf.insert(buf.end(), msg.bytes.begin(), msg.bytes.end());
		return buf;
	}

	static bool parse_rejoin(const uint8_t *data, size_t size, RejoinMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_u8(data, size, offset, msg.kind) || msg.kind > RejoinMsg::LOCKSTEP) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.total)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.reason)) return false;
		msg.bytes.assign(data + offset, data + size);
		return true;
	}

	static std::vector<uint8_t> frame_client_paused(const ClientPausedMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.guid);
		NetSerde::write_u8(buf, msg.pause ? 1 : 0);
		return buf;
	}

	static bool parse_client_paused(const uint8_t *data, size_t size, ClientPausedMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.guid)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.pause)) return false;
		return offset == size;
	}

	static std::vector<uint8_t> frame_kicked(const KickedMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_string(buf, msg.guid);
		NetSerde::write_string(buf, msg.name);
		NetSerde::write_u8(buf, msg.ban ? 1 : 0);
		return buf;
	}

	static bool parse_kicked(const uint8_t *data, size_t size, KickedMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.guid)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.name)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.ban)) return false;
		return offset == size;
	}

	static std::vector<uint8_t> frame_client_performance(const ClientPerformanceMsg &msg) {
		std::vector<uint8_t> buf;
		const size_t n = std::min<size_t>(msg.clients.size(), 255);
		NetSerde::write_u8(buf, (uint8_t)n);
		for (size_t i = 0; i < n; ++i) {
			NetSerde::write_u32(buf, msg.clients[i].client);
			NetSerde::write_u32(buf, msg.clients[i].rtt_ms);
			NetSerde::write_u32(buf, msg.clients[i].silent_ms);
		}
		return buf;
	}

	static bool parse_client_performance(const uint8_t *data, size_t size, ClientPerformanceMsg &msg) {
		size_t offset = 0;
		uint8_t n = 0;
		if (!NetSerde::read_u8(data, size, offset, n)) return false;
		msg.clients.assign(n, {});
		for (auto &e : msg.clients)
			if (!NetSerde::read_u32(data, size, offset, e.client) || !NetSerde::read_u32(data, size, offset, e.rtt_ms) ||
					!NetSerde::read_u32(data, size, offset, e.silent_ms)) return false;
		return offset == size;
	}

	static std::vector<uint8_t> frame_player_assignment(const PlayerAssignmentMsg &msg) {
		std::vector<uint8_t> buf;
		const size_t n = std::min<size_t>(msg.clients.size(), 255);
		NetSerde::write_u8(buf, (uint8_t)n);
		for (size_t i = 0; i < n; ++i) {
			NetSerde::write_u32(buf, msg.clients[i].client);
			NetSerde::write_string(buf, msg.clients[i].name);
			NetSerde::write_u8(buf, msg.clients[i].player);
			NetSerde::write_u8(buf, msg.clients[i].status);
		}
		return buf;
	}

	static bool parse_player_assignment(const uint8_t *data, size_t size, PlayerAssignmentMsg &msg) {
		size_t offset = 0;
		uint8_t n = 0;
		if (!NetSerde::read_u8(data, size, offset, n)) return false;
		msg.clients.assign(n, {});
		for (auto &e : msg.clients)
			if (!NetSerde::read_u32(data, size, offset, e.client) || !NetSerde::read_string(data, size, offset, e.name) ||
					!NetSerde::read_u8(data, size, offset, e.player) || !NetSerde::read_u8(data, size, offset, e.status) ||
					e.status > PlayerAssignmentMsg::LEFT) return false;
		return offset == size;
	}

	static std::vector<uint8_t> frame_join_sync(const JoinSyncMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u8(buf, msg.role);
		NetSerde::write_u32(buf, msg.requested_turn);
		return buf;
	}

	// Exact-size parse: an unknown role is rejected instead of being accepted and
	// then silently treated as ROLE_JOINER (which would let a peer ask for a resync).
	static bool parse_join_sync(const uint8_t *data, size_t size, JoinSyncMsg &msg) {
		if (size != 5) return false;
		size_t offset = 0;
		if (!NetSerde::read_u8(data, size, offset, msg.role)) return false;
		if (msg.role != JoinSyncMsg::ROLE_JOINER && msg.role != JoinSyncMsg::ROLE_HOST) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.requested_turn)) return false;
		return true;
	}

	static std::vector<uint8_t> frame_join_sync_check(const JoinSyncCheckMsg &msg) {
		std::vector<uint8_t> buf;
		NetSerde::write_u8(buf, msg.role);
		NetSerde::write_u8(buf, msg.accepted);
		NetSerde::write_u8(buf, msg.error);
		NetSerde::write_u32(buf, msg.turn);
		NetSerde::write_u32(buf, msg.hash);
		return buf;
	}

	static bool parse_join_sync_check(const uint8_t *data, size_t size, JoinSyncCheckMsg &msg) {
		if (size != 11) return false;
		size_t offset = 0;
		if (!NetSerde::read_u8(data, size, offset, msg.role)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.accepted)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.error)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.turn)) return false;
		if (!NetSerde::read_u32(data, size, offset, msg.hash)) return false;
		return true;
	}

	// Late-join answer, packed into the SAME 11-byte frame as
	// JoinSyncCheckMsg. Accepted replies carry {turn, hash} inside the hash/error
	// fields and are validated by TurnManager::receive_join_sync; only a rejected
	// reply is decoded as TurnStateSnapshotMsg (its Error is 1..3, never 0).
	static std::vector<uint8_t> frame_join_sync_snapshot(const TurnStateSnapshotMsg &msg) {
		JoinSyncCheckMsg c;
		c.role = JoinSyncMsg::ROLE_HOST;
		c.accepted = msg.accepted ? 1u : 0u;
		c.error = msg.error;
		c.turn = msg.turn;
		c.hash = msg.hash;
		return frame_join_sync_check(c);
	}

	static bool parse_join_sync_snapshot(const uint8_t *data, size_t size, TurnStateSnapshotMsg &msg) {
		JoinSyncCheckMsg c;
		if (!parse_join_sync_check(data, size, c)) return false;
		if (c.role != JoinSyncMsg::ROLE_HOST) return false;
		if (c.accepted && c.error != 0) return false; // accepted reply carries no error
		msg.turn = c.turn;
		msg.hash = c.hash;
		msg.accepted = (c.accepted != 0);
		msg.error = c.error;
		return true;
	}

	static std::vector<uint8_t> pack_message(NetMessageType type, uint32_t turn, const std::vector<uint8_t> &payload) {
		std::vector<uint8_t> buf;
		NetSerde::write_u8(buf, static_cast<uint8_t>(type));
		NetSerde::write_u32(buf, turn);
		NetSerde::write_u32(buf, static_cast<uint32_t>(payload.size()));
		if (!payload.empty()) {
			buf.insert(buf.end(), payload.begin(), payload.end());
		}
		return buf;
	}

	static bool unpack_message(const uint8_t *data, size_t size, NetMessage &out_msg) {
		size_t offset = 0;
		uint8_t type_val = 0;
		if (!NetSerde::read_u8(data, size, offset, type_val)) return false;
		out_msg.type = type_val;
		if (!NetSerde::read_u32(data, size, offset, out_msg.turn)) return false;
		uint32_t payload_len = 0;
		if (!NetSerde::read_u32(data, size, offset, payload_len)) return false;
		if (offset + payload_len > size) return false;
		out_msg.payload.assign(data + offset, data + offset + payload_len);
		return true;
	}
};

} // namespace rtso

