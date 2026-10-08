#pragma once
#include "net_types.h"
#include "../core/memory/arena.h"
#include <functional>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

namespace rtso {

// Network wire header flag constants
constexpr uint8_t NET_FLAG_ACK         = 0x01;
constexpr uint8_t NET_FLAG_CONNECT_REQ = 0x02;
constexpr uint8_t NET_FLAG_CONNECT_ACK = 0x04;
constexpr uint8_t NET_FLAG_DISCONNECT  = 0x08;
constexpr uint8_t NET_FLAG_RELIABLE    = 0x10;
constexpr uint8_t NET_FLAG_HEARTBEAT   = 0x20;

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

	// Packet transmission
	void sendCommand(uint32_t peer_id, const uint8_t *data, size_t size);
	void sendCommand(uint32_t peer_id, const std::vector<uint8_t> &data);
	void broadcastCommand(const uint8_t *data, size_t size, uint32_t exclude_peer = 0xFFFFFFFF);
	void broadcastCommand(const std::vector<uint8_t> &data, uint32_t exclude_peer = 0xFFFFFFFF);

	// Typed message framing helpers
	void sendSimulationCommand(uint32_t peer, uint32_t turn, const std::vector<uint8_t> &payload);
	void sendSyncCheck(uint32_t turn, uint32_t hash);
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
	// True once this host has been told its own NMT_GAME_START attributes, i.e.
	// it has an authoritative setup to hand a late joiner. NetHost never parses
	// the attributes itself (TurnManager owns the payload) but records the id.
	void note_game_start_sent() { game_start_sent_ = true; }
	bool game_start_sent() const { return game_start_sent_; }
	void sendHandshake();
	void sendAuthenticate(const std::string &name, const std::string &password = "");
	void sendReady(bool ready = true);
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

	// Callbacks
	void onCommand(std::function<void(uint32_t, const uint8_t*, size_t)> cb);
	void onMessage(std::function<void(uint32_t, const NetMessage&)> cb);
	void onPeerConnected(std::function<void(uint32_t)> cb);
	void onPeerDisconnected(std::function<void(uint32_t)> cb);
	void onChat(std::function<void(const ChatMsg&)> cb);
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

private:
	void send_raw_packet(const std::string &ip, uint16_t port, const uint8_t *data, size_t size);
	void send_wire_packet(const std::string &ip, uint16_t port, uint32_t peer_id, uint8_t flags, uint8_t channel, const uint8_t *payload, size_t payload_len, bool reliable);
	void handle_incoming_packet(const uint8_t *data, size_t size, const std::string &from_ip, uint16_t from_port);
	void send_ack(const std::string &ip, uint16_t port, uint32_t peer_id, uint32_t seq);
	void send_heartbeats(uint64_t now_ms);
	void check_retransmissions(uint64_t now_ms);
	void check_peer_timeouts(uint64_t now_ms);

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
	std::function<bool(uint32_t, uint32_t, uint32_t &, uint32_t &)> join_sync_request_cb_;

	std::vector<LobbyRoom> lobby_rooms_;
	std::string local_player_name_ = "Player";
	// True after this host sent NMT_GAME_START, so a late joiner is only answered
	// with an authoritative setup. Cleared by startHost/startClient/stop.
	bool game_start_sent_ = false;
};

} // namespace rtso

