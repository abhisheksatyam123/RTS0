#pragma once
#include "net_types.h"
#include "../core/memory/arena.h"
#include <functional>
#include <map>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <atomic>
#include <thread>

namespace rtso {

// Network wire header flag constants
constexpr uint8_t NET_FLAG_ACK         = 0x01;
constexpr uint8_t NET_FLAG_CONNECT_REQ = 0x02;
constexpr uint8_t NET_FLAG_CONNECT_ACK = 0x04;
constexpr uint8_t NET_FLAG_DISCONNECT  = 0x08;
constexpr uint8_t NET_FLAG_RELIABLE    = 0x10;
constexpr uint8_t NET_FLAG_HEARTBEAT   = 0x20;
// A reliable message too long for one datagram goes as several; every part but
// the last carries this flag (ENet's fragments, NetEnet.cpp: network.enetmtu).
constexpr uint8_t NET_FLAG_FRAGMENT    = 0x40;

#pragma pack(push, 1)
struct NetWireHeader {
	uint32_t magic = PS_PROTOCOL_MAGIC;
	uint32_t protocol_ver = PS_PROTOCOL_VERSION;
	uint32_t peer_id = 0;
	uint32_t seq = 0;
	uint32_t ack = 0;
	uint8_t  flags = 0;
	uint8_t  channel = 0;
	uint16_t payload_len = 0;
};
#pragma pack(pop)

struct PendingReliablePacket {
	uint32_t seq = 0;
	uint32_t peer_id = 0;
	std::string dest_ip;
	uint16_t dest_port = 0;
	std::vector<uint8_t> packet_data;
	uint64_t sent_time_ms = 0;
	uint64_t first_sent_ms = 0;
	int retry_count = 0;
};

class NetHost {
public:
	NetHost();
	~NetHost();

	// Socket lifecycle
	bool startHost(const NetConfig &c);
	bool startClient(const std::string &host, uint16_t port);
	void poll(Arena &arena);
	void stop();

	// Test hook: drop this fraction of outgoing datagrams (0 = off). Used by
	// tools/test_netcode.cpp --net-peer to prove reliable delivery survives loss.
	void set_simulated_loss(float fraction, uint32_t seed = 1) { sim_loss_ = fraction; sim_loss_rng_ = seed ? seed : 1; }

	// Test hook: a stateful firewall or restricted-cone NAT in front of this
	// socket. Datagrams from an endpoint this socket never sent to are dropped,
	// so a peer gets in only after a hole punch (tools/test_nat.cpp).
	void set_simulated_firewall(bool on);
	uint32_t firewalledPackets() const { return firewalled_; }

	// NAT traversal (0 A.D. StunClient.cpp and its Jingle hole punching).
	// Ask a STUN server, from this game socket, which public address and port
	// the NAT maps it to; works for a host and a client. stunState(): 0 never
	// asked, 1 waiting, 2 found (publicIp/publicPort), 3 no answer in
	// STUN_TIMEOUT_MS or the server name did not resolve.
	void queryStun(const std::string &server, uint16_t port);
	int stunState() const { return stun_state_; }
	const std::string &publicIp() const { return public_ip_; }
	uint16_t publicPort() const { return public_port_; }
	// Send another peer's public endpoint HOLE_PUNCH_SENDS small datagrams,
	// HOLE_PUNCH_DELAY_MS apart (lobby.stun.delay), so this side's NAT lets
	// that peer's packets in (0 A.D. SendHolePunchingMessages).
	void holePunch(const std::string &ip, uint16_t port);
	// This socket's local port, and this machine's LAN address (0 A.D.
	// StunClient::FindLocalIP; "" if there is no route).
	uint16_t localPort() const;
	static std::string localIp();
	const std::string &lanIp() const { return lan_ip_; } // localIp() as of queryStun
	// Client: connect to another address instead, keeping what is queued (0 A.D.
	// joins a host behind the same public address at its LAN address, since
	// not every router loops back to its own public address).
	void retargetHost(const std::string &ip, uint16_t port);
	static constexpr uint64_t STUN_RESEND_MS = 500;
	static constexpr uint64_t STUN_TIMEOUT_MS = 3000;
	static constexpr int HOLE_PUNCH_SENDS = 3;
	static constexpr uint64_t HOLE_PUNCH_DELAY_MS = 200;

	// Packet transmission
	void sendCommand(uint32_t peer_id, const uint8_t *data, size_t size);
	void sendCommand(uint32_t peer_id, const std::vector<uint8_t> &data);
	void broadcastCommand(const uint8_t *data, size_t size, uint32_t exclude_peer = 0xFFFFFFFF);
	void broadcastCommand(const std::vector<uint8_t> &data, uint32_t exclude_peer = 0xFFFFFFFF);

	// Typed message framing helpers
	void sendSimulationCommand(uint32_t peer, uint32_t turn, const std::vector<uint8_t> &payload);
	void sendSyncCheck(uint32_t turn, uint32_t hash);
	// receiver_guid "" = everyone, else peer_guid(id) of the one receiver.
	void sendChat(const std::string &msg, const std::string &receiver_guid = "");
	// Late-join turn-0 state resync (P3). Asks the host to resend the full
	// NMT_GAME_START setup plus its current turn state. Client-only; the host
	// answers through dispatchMessage() and a client replies with
	// NMT_JOIN_SYNC_CHECK either way.
	void request_join_sync(uint32_t requested_turn = 0);
	// Authoritative sender-id resolution for a relayed message.
	// See the peer_id==0 quirk note on resolve_sender_id() in net_host.cpp; wire
	// formats are unchanged, this only interprets the wire header peer_id.
	uint32_t resolve_sender_id(uint32_t wire_peer_id) const;
	// Host side of NMT_CHAT: stamp the sender, deliver to the receivers.
	void route_chat(ChatMsg chat, uint32_t author);
	// True once this host has been told its own NMT_GAME_START attributes, i.e.
	// it has an authoritative setup to hand a late joiner. NetHost never parses
	// the attributes itself (TurnManager owns the payload) but records the id.
	void note_game_start_sent() { game_start_sent_ = true; }
	// 0.28 polls ENet on its own thread "to avoid getting timeouts if the main
	// thread hangs" (NetSession.h). RTS0 polls in _process, so a step that
	// blocks it, the map load at Start, keeps its peers from timing it out by
	// sending heartbeats from a thread meanwhile. KeepAlive scopes that.
	void begin_keepalive();
	void end_keepalive();
	struct KeepAlive {
		NetHost &host;
		explicit KeepAlive(NetHost &h) : host(h) { host.begin_keepalive(); }
		~KeepAlive() { host.end_keepalive(); }
		KeepAlive(const KeepAlive &) = delete;
		KeepAlive &operator=(const KeepAlive &) = delete;
	};
	bool game_start_sent() const { return game_start_sent_; }
	void sendHandshake();
	void sendAuthenticate(const std::string &name, const std::string &password = "");
	// Client: 0 not ready, 1 ready for the host's setup with this
	// settings_version, 2 stay ready (0.28 ReadyController).
	void sendReady(uint8_t status = 1, uint32_t setup_version = 0);
	// Game setup before Start (see NetSetupState). Host: send the setup to
	// every admitted client (or to one); clear every client's ready flag but
	// stay ready (0 A.D. ClearAllPlayerReady). Client: the host's setups as they came
	// (newest by NetSetupState::seq wins); ask the host to change our own slot ("civ=3").
	void sendGameSetup(const std::string &attrs, uint32_t peer_id = 0xFFFFFFFF);
	void clearAllReady();
	std::vector<std::string> takeGameSetups() { std::vector<std::string> v; v.swap(game_setups_); return v; }
	void sendSetupRequest(const std::string &request);
	// Host: the clients' setup requests since the last call, oldest first.
	struct SetupRequest { uint32_t client; std::string request; };
	std::vector<SetupRequest> takeSetupRequests() { std::vector<SetupRequest> v; v.swap(setup_requests_); return v; }
	// Host: tell a client why its request was turned down. Client: those reasons.
	void sendSetupRefusal(uint32_t peer_id, const std::string &reason);
	std::vector<std::string> takeSetupRefusals() { std::vector<std::string> v; v.swap(setup_refusals_); return v; }
	void sendGameStart(const std::string &attributes);
	// Resend the SAME setup attributes to ONE peer (late-join resend). public so
	// TurnManager, which owns the payload it validated, can drive it.
	void sendGameStartTo(uint32_t peer_id, const std::string &attributes);

	struct LobbyRoom {
		std::string id;
		std::string host_ip;
		uint16_t host_port = 0;
		int players = 1;
		int max_players = 8;
		std::string map_id;
	};
	void publishLobbyRoom(const LobbyRoom &room);
	const std::vector<LobbyRoom> &lobbyRooms() const { return lobby_rooms_; }
	void clearLobbyRooms() { lobby_rooms_.clear(); }
	void setLocalPlayerName(const std::string &name) { local_player_name_ = name; }
	// Host: the password joiners must give (empty = open game; 0 A.D. lobby
	// password). Client: the password this client sends.
	void setPassword(const std::string &password) { password_ = password; }
	// 0 A.D. HashCryptographically(password, name): what travels instead of the
	// typed password (CNetClient::SetGamePassword), and what the host checks
	// against its own (CNetServerWorker::CheckPassword). Unset: the plain text.
	void setPasswordHasher(std::function<std::string(const std::string &password, const std::string &name)> hasher) {
		password_hasher_ = std::move(hasher);
	}
	// Host: 0 A.D. CNetServer(useLobbyAuth), on while hosting through the lobby.
	// Its handshake answer asks for lobby authentication
	// (PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH) and each joiner's NMT_AUTHENTICATE
	// waits: the verifier gets the peer and the lobby ticket the joiner sent,
	// and answers with finishLobbyAuth once the lobby named the ticket's owner
	// ("" for an unknown ticket), naming the ticket it was asked about. An
	// empty verifier turns it off.
	void setLobbyAuth(std::function<void(uint32_t peer_id, const std::string &ticket)> verifier) {
		lobby_auth_ = std::move(verifier);
	}
	bool lobbyAuth() const { return bool(lobby_auth_); }
	// Host: 0 A.D. OnAuthenticate's lobby check. The name the joiner sent,
	// without a " (rating)", must be the lobby's name for it, compared in lower
	// case; else it is refused with NDR_LOBBY_AUTH_FAILED. False if the peer
	// had no check waiting for that ticket.
	bool finishLobbyAuth(uint32_t peer_id, const std::string &ticket, const std::string &lobby_name);
	// Client: the ticket sent with NMT_AUTHENTICATE ("" for a direct join,
	// which a lobby host refuses: "Join via lobby.").
	void setLobbyTicket(const std::string &ticket) { lobby_ticket_ = ticket; }
	// The engine version and mods this side sends in its handshake (0 A.D.
	// engine version + enabled mods); a host refuses a client whose differs
	// (NDR_INCORRECT_SOFTWARE_VERSION).
	void setSoftwareVersion(const std::string &v) { software_version_ = v; }
	const std::string &softwareVersion() const { return software_version_; }
	// 0 A.D. CreateHandshake: the enabled mods (name, version) both sides
	// compare, less the ones marked ignoreInCompatibilityChecks.
	void setEnabledMods(std::vector<std::pair<std::string, std::string>> m) { enabled_mods_ = std::move(m); }
	// Client: the engine version the host's NMT_SERVER_HANDSHAKE named ("" before it came).
	const std::string &hostSoftwareVersion() const { return host_software_version_; }
	// Host: 0.28 network.duplicateplayernames. Off, a name already in the game
	// is refused (NDR_PLAYERNAME_IN_USE); on, the joiner becomes "Name (2)".
	void setDuplicatePlayerNames(bool on) { duplicate_names_ = on; }
	// Host: 0 A.D. SERVER_STATE_LOADING. While the started game loads, a joiner
	// is refused before anything else (NDR_SERVER_LOADING).
	void setServerLoading(bool on) { server_loading_ = on; }
	// Host: 0 A.D. NetServer::KickPlayer. Tells every client, drops the peer (the
	// lockstep goes on without it) and, with ban, refuses its name and address
	// from then on. False if there is no such admitted client.
	// Host: close a client's link once the answer just sent reaches it, with
	// no kick notice (0 A.D. NetServer Disconnect(NDR_SERVER_FULL)).
	// reason: the NDR_* the closing packet names (0.28 NetServer refusals).
	void refusePeer(uint32_t peer_id, uint8_t reason = 0);
	bool kickPeer(uint32_t peer_id, bool ban);
	// Host: the admitted client with this name (0 if none).
	uint32_t peerByName(const std::string &name) const;
	// Client: the host's answer to our authentication (ARC_*; -1 until it came).
	int authResult() const { return auth_result_; }
	// Every kick this peer was told about, and whether it was us.
	const std::vector<KickedMsg> &kicks() const { return kicks_; }
	bool kickedSelf() const { return kicked_self_; }
	// Client: the link to the host is gone (0 A.D. netstatus "disconnected"),
	// and why, as 0 A.D.'s NetHost.h NDR_* reasons (getDisconnectReason): 0 the
	// host fell silent, 2 the host ended the game, 3 another protocol version,
	// 6 kicked, 7 banned, 8 name in use, 9 server full, 14 wrong password,
	// 17 another engine version or mods. -1 while connected.
	bool connectionLost() const { return disconnect_reason_ >= 0; }
	int disconnectReason() const { return disconnect_reason_; }

	// Callbacks
	void onCommand(std::function<void(uint32_t, const uint8_t*, size_t)> cb);
	void onMessage(std::function<void(uint32_t, const NetMessage&)> cb);
	void onPeerConnected(std::function<void(uint32_t)> cb);
	void onPeerDisconnected(std::function<void(uint32_t)> cb);
	void onChat(std::function<void(const ChatMsg&)> cb);
	// Host: 0 A.D. NetServer OnChat sends an addressed line (ally, enemy,
	// observer, private, flare) only to its addressees. The router answers
	// whether `message` from client `from` goes to client `to`; unset, every
	// line goes to everyone and receivers filter (chat_routing.h).
	void setChatRouter(std::function<bool(uint32_t from, uint32_t to, const std::string &message)> router) {
		chat_router_ = std::move(router);
	}
	// Fired for a late-join request this host may answer. Returns true when the
	// owner answered it, filling turn_out / hash_out with the state it can prove a
	// joiner can rebuild; returns false to have NetHost reply ERR_JOINED_LATE.
	// Set by TurnManager::set_net_host.
	void onJoinSyncRequest(std::function<bool(uint32_t requester_peer_id, uint32_t requested_turn,
	                                          uint32_t &turn_out, uint32_t &hash_out)> cb);

	// Low-level message dispatcher
	void dispatchMessage(uint32_t peerId, const NetMessage &msg);

	// Status queries
	NetRole getRole() const { return role; }
	bool isHost() const { return role == NetRole::Host; }
	bool isClient() const { return role == NetRole::Client; }
	bool isConnected() const;
	const NetConfig& getConfig() const { return cfg; }
	const std::vector<NetPeer>& getPeers() const { return peers_; }
	const NetPeer* getPeer(uint32_t peer_id) const;
	NetPeer* getPeer(uint32_t peer_id);
	const NetStats& getStats() const { return stats_; }
	uint32_t getLocalPeerId() const { return local_peer_id_; }
	// Client: milliseconds since anything came from the host (0 A.D.
	// GetLastReceivedTime, for the "Losing connection to server" warning).
	uint32_t hostSilentMs() const;
	// How every connected client is doing (0 A.D. NMT_CLIENT_PERFORMANCE): the
	// host measures it, a client has the host's latest report.
	const std::vector<ClientPerformanceMsg::Entry> &clientPerformance() const { return client_perf_; }
	uint64_t clientPerformanceReports() const { return client_perf_reports_; }
	// NMT_PLAYER_ASSIGNMENT: the host sends the match's client list to every
	// client (reliable) and keeps it; a client keeps the host's latest one.
	void sendPlayerAssignment(const PlayerAssignmentMsg &m);
	const std::vector<PlayerAssignmentMsg::Entry> &playerAssignment() const { return player_assignment_; }
	uint64_t playerAssignments() const { return player_assignments_; }
	void setPeerObserver(uint32_t peer_id, bool observer);
	const std::string &localPlayerName() const { return local_player_name_; }

private:
	void send_raw_packet(const std::string &ip, uint16_t port, const uint8_t *data, size_t size);
	void send_wire_packet(const std::string &ip, uint16_t port, uint32_t peer_id, uint8_t flags, uint8_t channel, const uint8_t *payload, size_t payload_len, bool reliable);
	void handle_incoming_packet(const uint8_t *data, size_t size, const std::string &from_ip, uint16_t from_port);
	// The part of a packet that follows the reliable-order check: disconnect,
	// fragment reassembly and the message itself.
	void deliver_packet(uint8_t flags, const uint8_t *payload, size_t payload_size, uint32_t sender_peer_id);
	// Delivers the held reliable packets that are next in order; with skip, the
	// first one even across a gap that outlived kHoldGapMs.
	void release_held(NetPeer &p, bool skip);
	void check_held(uint64_t now_ms);
public:
	// The largest message payload one datagram carries: 0.28's ENet MTU (1372)
	// less its headers, so no datagram relies on IP fragmentation.
	static constexpr size_t kFragmentPayload = 1200;
	// A gap in a reliable stream is filled by a resend, within seconds even on a
	// lossy link; one that lasts this long (the sender gave up) is skipped.
	static constexpr uint64_t kHoldGapMs = 30000;
	// Reliable packets from peer_id that arrived early and wait for a resend of
	// an earlier one: the stream is still coming, only held back.
	size_t held_count(uint32_t peer_id) const {
		const NetPeer *p = getPeer(peer_id);
		return p ? p->held_packets.size() : 0;
	}
private:
	void send_ack(const std::string &ip, uint16_t port, uint32_t peer_id, uint32_t seq);
	void send_heartbeats(uint64_t now_ms);
	void check_retransmissions(uint64_t now_ms);
	void check_peer_timeouts(uint64_t now_ms);
	void send_client_performance(uint64_t now_ms);
	bool admitted(const NetPeer &p) const { return p.connected && p.is_authenticated && !p.kick_disconnect_ms; }
	std::string password_;
	std::function<std::string(const std::string &, const std::string &)> password_hasher_;
	std::function<void(uint32_t, const std::string &)> lobby_auth_;
	std::string lobby_ticket_;
	std::map<uint32_t, AuthenticateMsg> lobby_auth_pending_; // host: joiners waiting on the lobby
	void authenticate_peer(NetPeer &p, AuthenticateMsg auth);
public:
	static std::string sanitise_player_name(const std::string &original);
private:
	std::string software_version_ = NET_SOFTWARE_VERSION;
	std::vector<std::pair<std::string, std::string>> enabled_mods_;
	std::string host_software_version_;
	bool duplicate_names_ = false;
	bool server_loading_ = false;
	// Host: close a link with this reason, sent in the disconnect's channel byte.
	void refuse_with(NetPeer &p, uint8_t reason);
	bool name_taken(const std::string &name, const NetPeer *self) const;
	std::set<std::string> banned_names_, banned_addresses_;
	// 0 A.D. NetServer m_FailedAttempts: wrong passwords per name (the address
	// for an unnamed client); FAILED_PASSWORD_TRIES_BEFORE_BAN of them bans it,
	// and the right password before that resets the count.
	std::map<std::string, int> failed_passwords_;
	static constexpr int FAILED_PASSWORD_TRIES_BEFORE_BAN = 3;
	int auth_result_ = -1;
	std::vector<KickedMsg> kicks_;
	bool kicked_self_ = false;
	bool banned_self_ = false;
	int disconnect_reason_ = -1;
	void lose_host(int reason);
	// The disconnect notice is a bare unreliable datagram (ENet's waits for an
	// ack); a few copies make a lost one unlikely, and repeats are harmless.
	static constexpr int kDisconnectSends = 3;
	// A client gives up on a host that never answers its connect after this long.
	static constexpr uint64_t kConnectTimeoutMs = 11000;
	std::vector<ClientPerformanceMsg::Entry> client_perf_;
	uint64_t client_perf_reports_ = 0;
	uint64_t last_perf_ms_ = 0;
	std::vector<std::string> game_setups_; // client: the host's NMT_GAME_SETUPs not yet taken
	std::vector<SetupRequest> setup_requests_;
	std::vector<std::string> setup_refusals_; // client: the host's "refused=" answers not yet taken
	std::vector<PlayerAssignmentMsg::Entry> player_assignment_;
	uint64_t player_assignments_ = 0;

	NetConfig cfg;
	NetRole role = NetRole::None;
	int64_t socket_fd_ = -1;
	uint32_t local_peer_id_ = 0;
	float sim_loss_ = 0.0f;
	uint32_t sim_loss_rng_ = 1;
	uint32_t next_peer_id_ = 1;
	uint32_t next_seq_ = 1;
	uint64_t last_heartbeat_ms_ = 0;

	std::string server_ip_;
	uint16_t server_port_ = 0;

	NetStats stats_;
	std::vector<NetPeer> peers_;
	std::vector<PendingReliablePacket> pending_acks_;

	std::function<void(uint32_t, const uint8_t*, size_t)> command_cb_;
	std::function<void(uint32_t, const NetMessage&)> message_cb_;
	std::function<void(uint32_t)> peer_connected_cb_;
	std::function<void(uint32_t)> peer_disconnected_cb_;
	std::function<void(const ChatMsg&)> chat_cb_;
	std::function<bool(uint32_t, uint32_t, const std::string &)> chat_router_;
	std::function<bool(uint32_t, uint32_t, uint32_t &, uint32_t &)> join_sync_request_cb_;

	void check_stun(uint64_t now_ms);
	void send_hole_punches(uint64_t now_ms);
	bool sim_firewall_ = false;
	uint32_t firewalled_ = 0;
	std::set<std::string> sent_to_; // "ip:port" this socket sent to (sim firewall)
	int stun_state_ = 0;
	std::string stun_server_ip_;
	uint16_t stun_server_port_ = 0;
	uint8_t stun_txn_[12] = {};
	uint64_t stun_started_ms_ = 0, stun_sent_ms_ = 0;
	std::string public_ip_, lan_ip_;
	uint16_t public_port_ = 0;
	struct HolePunch { std::string ip; uint16_t port = 0; uint64_t next_ms = 0; int left = 0; };
	std::vector<HolePunch> hole_punches_;

	std::vector<LobbyRoom> lobby_rooms_;
	std::string local_player_name_ = "Player";
	// True after this host sent NMT_GAME_START, so a late joiner is only answered
	// with an authoritative setup. Cleared by startHost/startClient/stop.
	bool game_start_sent_ = false;
	std::thread keepalive_;
	std::atomic<bool> keepalive_stop_{false};
};

} // namespace rtso

