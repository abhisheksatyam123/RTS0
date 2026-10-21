#include "net_host.h"
#include "stun.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET(s) closesocket(s)
#define IS_SOCKET_VALID(s) ((s) != INVALID_SOCKET)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#define CLOSE_SOCKET(s) ::close(s)
#define INVALID_SOCKET (-1)
#define IS_SOCKET_VALID(s) ((s) >= 0)
typedef int SOCKET;
#endif

namespace rtso {

static uint64_t get_now_ms() {
	using namespace std::chrono;
	return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static void ensure_socket_subsystem() {
#ifdef _WIN32
	static bool initialized = false;
	if (!initialized) {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
		initialized = true;
	}
#endif
}

static bool set_socket_nonblocking(int64_t fd) {
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0;
#else
	int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

NetHost::NetHost() {
	ensure_socket_subsystem();
}

NetHost::~NetHost() {
	stop();
}

void NetHost::begin_keepalive() {
	end_keepalive();
	if (socket_fd_ < 0) return;
	// Everything the thread needs is copied here: it touches only the socket.
	NetWireHeader hdr;
	hdr.peer_id = local_peer_id_;
	hdr.flags = NET_FLAG_HEARTBEAT;
	std::vector<std::pair<std::string, uint16_t>> to;
	if (isClient() && !server_ip_.empty()) to.emplace_back(server_ip_, server_port_);
	else if (isHost())
		for (const auto &p : peers_)
			if (p.connected) to.emplace_back(p.address, p.port);
	std::vector<sockaddr_in> dests;
	for (const auto &[ip, port] : to) {
		sockaddr_in d{};
		d.sin_family = AF_INET;
		d.sin_port = htons(port);
		if (inet_pton(AF_INET, ip.c_str(), &d.sin_addr) > 0) dests.push_back(d);
	}
	if (dests.empty()) return;
	keepalive_stop_ = false;
	const int fd = socket_fd_;
	keepalive_ = std::thread([this, fd, hdr, dests] {
		while (!keepalive_stop_) {
			for (const auto &d : dests)
				sendto(static_cast<SOCKET>(fd), reinterpret_cast<const char*>(&hdr), sizeof(hdr), 0,
						reinterpret_cast<const struct sockaddr*>(&d), sizeof(d));
			for (int i = 0; i < 20 && !keepalive_stop_; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	});
}

void NetHost::end_keepalive() {
	if (!keepalive_.joinable()) return;
	keepalive_stop_ = true;
	keepalive_.join();
}

bool NetHost::startHost(const NetConfig &c) {
	stop();
	ensure_socket_subsystem();
	cfg = c;

	SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!IS_SOCKET_VALID(fd)) {
		std::cerr << "[NetHost] Failed to create UDP socket for host\n";
		return false;
	}

	// No SO_REUSEADDR (ENet's server socket has none): a second host on the
	// same port must fail to bind rather than share it and miss the joiners.
	set_socket_nonblocking(fd);

	sockaddr_in bind_addr{};
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(c.port);

	if (bind(fd, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
		std::cerr << "[NetHost] Failed to bind to port " << c.port << "\n";
		CLOSE_SOCKET(fd);
		return false;
	}

	socket_fd_ = static_cast<int64_t>(fd);
	role = NetRole::Host;
	local_peer_id_ = 0;
	next_peer_id_ = 1;
	banned_names_.clear();
	banned_addresses_.clear();
	failed_passwords_.clear();
	lobby_auth_pending_.clear();
	kicks_.clear();
	next_seq_ = 1;
	peers_.clear();
	pending_acks_.clear();
	last_heartbeat_ms_ = get_now_ms();
	game_start_sent_ = false;
	game_setups_.clear();
	setup_requests_.clear();
	setup_refusals_.clear();

	lobby_rooms_.clear();
	LobbyRoom room;
	room.id = "room-" + std::to_string(c.port);
	room.host_ip = "0.0.0.0";
	room.host_port = c.port;
	room.players = 1;
	room.max_players = c.maxPlayers;
	room.map_id = "default";
	lobby_rooms_.push_back(room);

	std::cout << "[NetHost] Host listening on port " << c.port << " (lobby " << room.id << ")\n";
	return true;
}

void NetHost::publishLobbyRoom(const LobbyRoom &room) {
	for (auto &existing : lobby_rooms_) {
		if (existing.id == room.id ||
		    (existing.host_port == room.host_port && existing.host_ip == room.host_ip)) {
			existing = room;
			return;
		}
	}
	lobby_rooms_.push_back(room);
}

bool NetHost::startClient(const std::string &host, uint16_t p) {
	stop();
	ensure_socket_subsystem();
	// CNetClientSession::Connect: the host's name is resolved once, and a name
	// that does not resolve fails the join at once.
	std::string h = host;
	in_addr numeric{};
	if (inet_pton(AF_INET, host.c_str(), &numeric) <= 0) {
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		addrinfo *res = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
			std::cerr << "[NetHost] Cannot resolve " << host << "\n";
			return false;
		}
		char ip[INET_ADDRSTRLEN] = {0};
		inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr, ip, sizeof(ip));
		freeaddrinfo(res);
		h = ip;
	}
	server_ip_ = h;
	server_port_ = p;

	SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!IS_SOCKET_VALID(fd)) {
		std::cerr << "[NetHost] Failed to create UDP socket for client\n";
		return false;
	}

	set_socket_nonblocking(fd);

	sockaddr_in bind_addr{};
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(0);
	bind(fd, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr));

	socket_fd_ = static_cast<int64_t>(fd);
	role = NetRole::Client;
	local_peer_id_ = 0;
	auth_result_ = -1;
	kicks_.clear();
	kicked_self_ = false;
	banned_self_ = false;
	disconnect_reason_ = -1;
	host_software_version_.clear();
	next_seq_ = 1;
	peers_.clear();
	pending_acks_.clear();
	last_heartbeat_ms_ = get_now_ms();
	game_start_sent_ = false;
	game_setups_.clear();
	setup_requests_.clear();
	setup_refusals_.clear();

	NetPeer host_peer;
	host_peer.id = 0;
	host_peer.address = h;
	host_peer.port = p;
	host_peer.connected = false;
	host_peer.last_seen_ms = get_now_ms();
	peers_.push_back(host_peer);

	// Send initial connection request with client handshake
	ClientHandshakeMsg chm;
	chm.engine_version = software_version_;
	chm.enabled_mods = enabled_mods_;
	std::vector<uint8_t> chm_payload = NetPacketSerializer::frame_client_handshake(chm);
	std::vector<uint8_t> chm_msg = NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_HANDSHAKE, 0, chm_payload);

	send_wire_packet(server_ip_, server_port_, 0, NET_FLAG_CONNECT_REQ, cfg.commandChannel, chm_msg.data(), chm_msg.size(), true);

	std::cout << "[NetHost] Client connecting to " << h << ":" << p << "\n";
	return true;
}

bool NetHost::isConnected() const {
	if (role == NetRole::Host) {
		return socket_fd_ >= 0;
	}
	if (role == NetRole::Client) {
		return socket_fd_ >= 0 && !peers_.empty() && peers_[0].connected;
	}
	return false;
}

const NetPeer* NetHost::getPeer(uint32_t peer_id) const {
	for (const auto &p : peers_) {
		if (p.id == peer_id) return &p;
	}
	return nullptr;
}

NetPeer* NetHost::getPeer(uint32_t peer_id) {
	for (auto &p : peers_) {
		if (p.id == peer_id) return &p;
	}
	return nullptr;
}

// --- G9: the sender peer_id==0 quirk ----------------------------------------
// A client puts `hdr.peer_id = local_peer_id_` on the wire and leaves it 0 until
// it sees the host's NET_FLAG_CONNECT_ACK, which carries the id the host assigned
// (net_host.cpp startHost: next_peer_id_ starts at 1, the host itself is 0). So a
// client's FIRST few datagrams legitimately carry peer_id 0, and the host rewrites
// that to the table-assigned id when it accepts the peer -- which is why
// handle_incoming_packet's fallbacks (`scm.client_id != 0 ? scm.client_id : peer_id`)
// work at all. The residue is that peer_id 0 is AMBIGUOUS:
//
//   * the host's own id is 0 (startHost), and a client represents the host in its
//     peer table as id 0 -- but handle_incoming_packet passes 0 to peer_connected_cb_
//     for a client's own connection, so 0 is never a *remote* client id;
//   * a client datagram that has not been acked yet still claims 0 on the wire.
//
// Callbacks therefore cannot trust a bare 0. resolve_sender_id() resolves it:
//
//   HOST   : 0 means "me". Any other value is looked up in the peer table and
//            replaced with the table id, because the wire value is whatever the
//            sender last believed and the table is authoritative. Unknown -> 0.
//   CLIENT : 0 is the host (the only remote party RTS0 supports, and the only id
//            present in peers_[0]). Anything else stays as received.
//
// The wire format is untouched -- ids stay append-only, cmd_type values stay
// stable, and no message payload carries a sender id: this is purely an
// interpretation rule for the existing NetWireHeader::peer_id field.
uint32_t NetHost::resolve_sender_id(uint32_t wire_peer_id) const {
	if (isClient()) {
		// Single remote party (the host). A stale/unknown non-zero value is
		// resolved through the table; the host is always peers_[0] with id 0.
		if (wire_peer_id == 0) return 0;
		for (const auto &p : peers_) {
			if (p.id != 0 && p.id == wire_peer_id) return 0;
		}
		return wire_peer_id;
	}
	// Host: id 0 is the host itself.
	if (wire_peer_id == 0) return 0;
	for (const auto &p : peers_) {
		if (p.id == wire_peer_id) return p.id;
	}
	return 0;
}

void NetHost::request_join_sync(uint32_t requested_turn) {
	if (!isClient() || server_ip_.empty()) return;
	JoinSyncMsg jsm;
	jsm.role = JoinSyncMsg::ROLE_JOINER;
	jsm.requested_turn = requested_turn;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_join_sync(jsm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_JOIN_SYNC_STATE, 0, payload);
	broadcastCommand(msg.data(), msg.size());
}

static std::vector<uint8_t> join_sync_reply_packet(uint32_t role, uint32_t turn,
                                                   uint8_t accepted, uint32_t hash,
                                                   uint8_t error) {
	JoinSyncCheckMsg c;
	c.role = (uint8_t)role;
	c.accepted = accepted;
	c.error = error;
	c.turn = turn;
	c.hash = hash;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_join_sync_check(c);
	return NetPacketSerializer::pack_message(NetMessageType::NMT_JOIN_SYNC_STATE, 0, payload);
}

void NetHost::send_raw_packet(const std::string &ip, uint16_t port, const uint8_t *data, size_t size) {
	if (socket_fd_ < 0) return;
	if (sim_firewall_) sent_to_.insert(ip + ":" + std::to_string(port));
	if (sim_loss_ > 0.0f) {
		sim_loss_rng_ ^= sim_loss_rng_ << 13;
		sim_loss_rng_ ^= sim_loss_rng_ >> 17;
		sim_loss_rng_ ^= sim_loss_rng_ << 5;
		if ((float)(sim_loss_rng_ & 0xFFFFFF) / 16777216.0f < sim_loss_) return;
	}
	sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) <= 0) {
		struct hostent *he = gethostbyname(ip.c_str());
		if (he && he->h_addr_list && he->h_addr_list[0]) {
			std::memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(dest.sin_addr));
		} else {
			return;
		}
	}
	ssize_t sent = sendto(static_cast<SOCKET>(socket_fd_), reinterpret_cast<const char*>(data), size, 0, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
	if (sent > 0) {
		stats_.sent_bytes += static_cast<uint32_t>(sent);
		stats_.sent_packets++;
	}
}

void NetHost::send_wire_packet(const std::string &ip, uint16_t port, uint32_t peer_id, uint8_t flags, uint8_t channel, const uint8_t *payload, size_t payload_len, bool reliable) {
	if (socket_fd_ < 0) return;
	if (reliable && payload_len > kFragmentPayload) {
		// ENet fragments: consecutive reliable parts, all but the last flagged.
		for (size_t off = 0; off < payload_len; off += kFragmentPayload) {
			const size_t n = std::min(kFragmentPayload, payload_len - off);
			const bool last = off + n >= payload_len;
			send_wire_packet(ip, port, peer_id, last ? flags : (uint8_t)(NET_FLAG_FRAGMENT | (flags & ~NET_FLAG_DISCONNECT)),
					channel, payload + off, n, true);
		}
		return;
	}

	NetWireHeader hdr;
	hdr.magic = PS_PROTOCOL_MAGIC;
	hdr.protocol_ver = PS_PROTOCOL_VERSION;
	// A connect ACK carries the id the host gave the client (0 A.D.
	// NMT_AUTHENTICATE_RESULT's host id); everything else carries the sender's.
	hdr.peer_id = (flags & NET_FLAG_CONNECT_ACK) && isHost() ? peer_id : local_peer_id_;
	// Reliable seqs are numbered per destination so the receiver sees a gapless
	// stream and can tell a retransmit from a packet it never got.
	if (reliable) {
		NetPeer *dest_peer = getPeer(peer_id);
		hdr.seq = dest_peer ? ++dest_peer->last_sent_seq : next_seq_++;
	}
	hdr.ack = 0;
	hdr.flags = flags;
	if (reliable) {
		hdr.flags |= NET_FLAG_RELIABLE;
	}
	hdr.channel = channel;
	hdr.payload_len = static_cast<uint16_t>(payload_len);

	std::vector<uint8_t> packet;
	packet.resize(sizeof(NetWireHeader) + payload_len);
	std::memcpy(packet.data(), &hdr, sizeof(NetWireHeader));
	if (payload && payload_len > 0) {
		std::memcpy(packet.data() + sizeof(NetWireHeader), payload, payload_len);
	}

	send_raw_packet(ip, port, packet.data(), packet.size());

	if (reliable) {
		PendingReliablePacket prp;
		prp.seq = hdr.seq;
		prp.peer_id = peer_id;
		prp.dest_ip = ip;
		prp.dest_port = port;
		prp.packet_data = std::move(packet);
		prp.sent_time_ms = get_now_ms();
		prp.first_sent_ms = prp.sent_time_ms;
		prp.retry_count = 0;
		pending_acks_.push_back(std::move(prp));
	}
}

void NetHost::send_ack(const std::string &ip, uint16_t port, uint32_t peer_id, uint32_t seq) {
	NetWireHeader hdr;
	hdr.magic = PS_PROTOCOL_MAGIC;
	hdr.protocol_ver = PS_PROTOCOL_VERSION;
	hdr.peer_id = local_peer_id_;
	hdr.seq = 0;
	hdr.ack = seq;
	hdr.flags = NET_FLAG_ACK;
	hdr.channel = 0;
	hdr.payload_len = 0;
	(void)peer_id;
	send_raw_packet(ip, port, reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
}

void NetHost::sendCommand(uint32_t peer_id, const uint8_t *data, size_t size) {
	if (socket_fd_ < 0) return;
	const NetPeer *peer = getPeer(peer_id);
	if (!peer) {
		if (isClient() && !server_ip_.empty()) {
			send_wire_packet(server_ip_, server_port_, 0, 0, cfg.commandChannel, data, size, true);
		}
		return;
	}
	send_wire_packet(peer->address, peer->port, peer->id, 0, cfg.commandChannel, data, size, true);
}

void NetHost::sendCommand(uint32_t peer_id, const std::vector<uint8_t> &data) {
	sendCommand(peer_id, data.data(), data.size());
}

void NetHost::broadcastCommand(const uint8_t *data, size_t size, uint32_t exclude_peer) {
	if (socket_fd_ < 0) return;
	if (isClient()) {
		if (!server_ip_.empty()) {
			send_wire_packet(server_ip_, server_port_, 0, 0, cfg.commandChannel, data, size, true);
		}
	} else if (isHost()) {
		for (const auto &peer : peers_) {
			if (admitted(peer) && peer.id != exclude_peer) {
				send_wire_packet(peer.address, peer.port, peer.id, 0, cfg.commandChannel, data, size, true);
			}
		}
	}
}

void NetHost::broadcastCommand(const std::vector<uint8_t> &data, uint32_t exclude_peer) {
	broadcastCommand(data.data(), data.size(), exclude_peer);
}

void NetHost::sendSimulationCommand(uint32_t peer, uint32_t turn, const std::vector<uint8_t> &payload) {
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_SIMULATION_COMMAND, turn, payload);
	if (peer == 0xFFFFFFFF) {
		broadcastCommand(msg.data(), msg.size());
	} else {
		sendCommand(peer, msg.data(), msg.size());
	}
}

void NetHost::sendSyncCheck(uint32_t turn, uint32_t hash) {
	SyncCheckMsg scm;
	scm.turn = turn;
	scm.hash = hash;
	scm.hash_str = std::to_string(hash);
	std::vector<uint8_t> payload = NetPacketSerializer::frame_sync_check(scm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_SYNC_CHECK, turn, payload);
	broadcastCommand(msg.data(), msg.size());
}

void NetHost::sendChat(const std::string &message, const std::string &receiver_guid) {
	ChatMsg cm;
	cm.sender_guid = peer_guid(local_peer_id_);
	cm.message = message;
	if (!receiver_guid.empty()) {
		cm.receivers.push_back(receiver_guid);
	}
	if (isHost()) {
		route_chat(cm, local_peer_id_);
		return;
	}
	// A client sends to the host, which stamps the sender and routes it.
	std::vector<uint8_t> payload = NetPacketSerializer::frame_chat(cm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_CHAT, 0, payload);
	broadcastCommand(msg.data(), msg.size());
}

// 0 A.D. NetServer.cpp OnChat: the server overwrites the sender GUID with the
// session's own (so nobody can speak as someone else), then multicasts to the
// listed receivers, or to everyone when the list is empty. The sender keeps its
// local echo, so it is never sent its own line back.
void NetHost::route_chat(ChatMsg chat, uint32_t author) {
	chat.sender_guid = peer_guid(author);
	const std::vector<std::string> receivers = std::move(chat.receivers);
	chat.receivers.clear();
	auto wants = [&](uint32_t id) {
		if (chat_router_ && !chat_router_(author, id, chat.message)) return false;
		return receivers.empty() || std::find(receivers.begin(), receivers.end(), peer_guid(id)) != receivers.end();
	};
	if (author != local_peer_id_ && wants(local_peer_id_) && chat_cb_) chat_cb_(chat);
	std::vector<uint8_t> payload = NetPacketSerializer::frame_chat(chat);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_CHAT, 0, payload);
	for (const auto &p : peers_) {
		if (admitted(p) && p.id != author && wants(p.id)) sendCommand(p.id, pkt.data(), pkt.size());
	}
}

void NetHost::sendHandshake() {
	if (isClient()) {
		ClientHandshakeMsg chm;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_client_handshake(chm);
		std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_HANDSHAKE, 0, payload);
		broadcastCommand(msg.data(), msg.size());
	} else if (isHost()) {
		ServerHandshakeMsg shm;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_server_handshake(shm);
		std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_SERVER_HANDSHAKE, 0, payload);
		broadcastCommand(msg.data(), msg.size());
	}
}

void NetHost::sendAuthenticate(const std::string &name, const std::string &password) {
	AuthenticateMsg am;
	am.name = name;
	am.password = password.empty() || !password_hasher_ ? password : password_hasher_(password, name);
	am.lobby_ticket = lobby_ticket_;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_authenticate(am);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_AUTHENTICATE, 0, payload);
	broadcastCommand(msg.data(), msg.size());
}

void NetHost::sendReady(uint8_t status, uint32_t setup_version) {
	ReadyMsg rm;
	rm.guid = "peer-" + std::to_string(local_peer_id_);
	rm.status = status <= 2 ? status : 1;
	rm.setup_version = setup_version;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_ready(rm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_READY, 0, payload);
	broadcastCommand(msg.data(), msg.size());
}

void NetHost::sendGameSetup(const std::string &attrs, uint32_t peer_id) {
	if (!isHost()) return;
	GameStartMsg m;
	m.init_attributes = attrs;
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_SETUP, 0,
			NetPacketSerializer::frame_game_start(m));
	if (peer_id == 0xFFFFFFFF) broadcastCommand(msg.data(), msg.size());
	else sendCommand(peer_id, msg.data(), msg.size());
}

void NetHost::clearAllReady() {
	for (auto &p : peers_)
		if (p.ready_status != 2) p.ready_status = 0;
}

void NetHost::sendSetupRequest(const std::string &request) {
	if (!isClient()) return;
	GameStartMsg m;
	m.init_attributes = request;
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_SETUP_REQUEST, 0,
			NetPacketSerializer::frame_game_start(m));
	broadcastCommand(msg.data(), msg.size());
}

void NetHost::sendSetupRefusal(uint32_t peer_id, const std::string &reason) {
	if (!isHost()) return;
	GameStartMsg m;
	m.init_attributes = "refused=" + reason;
	sendCommand(peer_id, NetPacketSerializer::pack_message(NetMessageType::NMT_SETUP_REQUEST, 0,
			NetPacketSerializer::frame_game_start(m)));
}

void NetHost::sendGameStart(const std::string &attributes) {
	GameStartMsg gsm;
	gsm.init_attributes = attributes;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
	broadcastCommand(msg.data(), msg.size());
	// A sent GAME_START is this host's authoritative setup; remember it so a peer
	// admitted later can be answered (see request_join_sync / dispatchMessage).
	if (isHost()) game_start_sent_ = true;
}

// Resend the authoritative setup to ONE admitted peer. Used by the late-join
// path: the attributes are the same ones the host broadcast at Start, so this is
// a pure replay and cannot diverge. public for TurnManager, which owns the payload
// it hands here.
void NetHost::sendGameStartTo(uint32_t peer_id, const std::string &attributes) {
	GameStartMsg gsm;
	gsm.init_attributes = attributes;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
	sendCommand(peer_id, msg.data(), msg.size());
}

void NetHost::onCommand(std::function<void(uint32_t, const uint8_t*, size_t)> cb) {
	command_cb_ = std::move(cb);
}

void NetHost::onMessage(std::function<void(uint32_t, const NetMessage&)> cb) {
	message_cb_ = std::move(cb);
}

void NetHost::onPeerConnected(std::function<void(uint32_t)> cb) {
	peer_connected_cb_ = std::move(cb);
}

void NetHost::onPeerDisconnected(std::function<void(uint32_t)> cb) {
	peer_disconnected_cb_ = std::move(cb);
}

void NetHost::onChat(std::function<void(const ChatMsg&)> cb) {
	chat_cb_ = std::move(cb);
}

void NetHost::onJoinSyncRequest(std::function<bool(uint32_t, uint32_t, uint32_t &, uint32_t &)> cb) {
	join_sync_request_cb_ = std::move(cb);
}

void NetHost::handle_incoming_packet(const uint8_t *data, size_t size, const std::string &from_ip, uint16_t from_port) {
	if (sim_firewall_ && !sent_to_.count(from_ip + ":" + std::to_string(from_port))) {
		firewalled_++;
		return;
	}
	if (stun::is_stun(data, size)) {
		std::string ip;
		uint16_t port = 0;
		if (stun_state_ == 1 && stun::parse_binding_success(data, size, stun_txn_, ip, port)) {
			public_ip_ = ip;
			public_port_ = port;
			stun_state_ = 2;
			std::cout << "[NetHost] STUN: public endpoint " << ip << ":" << port << "\n";
		}
		return;
	}
	if (stun::is_hole_punch(data, size)) return; // it only opened the sender's NAT
	if (size < sizeof(NetWireHeader)) return;

	NetWireHeader hdr;
	std::memcpy(&hdr, data, sizeof(NetWireHeader));
	if (hdr.magic != PS_PROTOCOL_MAGIC && hdr.magic != 0x52545330) {
		stats_.dropped_packets++;
		return;
	}

	stats_.recv_packets++;
	stats_.recv_bytes += static_cast<uint32_t>(size);
	uint64_t now_ms = get_now_ms();

	uint32_t sender_peer_id = hdr.peer_id;

	if (isHost()) {
		// Only a session still open speaks for its address: a client that left
		// keeps its entry, and the OS can hand its port to a new client, whose
		// connect must then open a session of its own (not be told the old id).
		NetPeer *peer = nullptr;
		for (auto &p : peers_) {
			if (p.connected && p.address == from_ip && p.port == from_port) {
				peer = &p;
				break;
			}
		}

		if (!peer && (hdr.flags & NET_FLAG_CONNECT_REQ)) {
			// Sessions that left keep their entry (ids stay unique), so count
			// only the connected ones, as 0 A.D. counts m_Sessions.
			const size_t live = (size_t)std::count_if(peers_.begin(), peers_.end(),
					[](const NetPeer &p) { return p.connected; });
			if (live >= NET_MAX_CLIENTS) {
				// NDR_SERVER_FULL, in the disconnect's channel byte; sent each
				// time it asks, since the client resends its connect.
				send_wire_packet(from_ip, from_port, 0, NET_FLAG_DISCONNECT, NDR_SERVER_FULL, nullptr, 0, false);
			} else {
				NetPeer np;
				// Before Start, a joiner takes the lowest id no connected peer holds:
				// the setup seats and lists clients 0 to NET_SETUP_CLIENTS-1 (64-bit
				// masks), so ids burnt by leavers and refused passwords must not
				// push it past them.
				// 0 A.D. keys assignments by GUID instead; after Start ids stay
				// unique, since the turn manager still knows a dropped client's id.
				uint32_t id = 0;
				if (!game_start_sent_)
					for (uint32_t c = 1; c < NET_SETUP_CLIENTS && !id; ++c)
						if (std::none_of(peers_.begin(), peers_.end(),
									[c](const NetPeer &p) { return p.id == c && p.connected; }))
							id = c;
				if (id) {
					peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
										 [id](const NetPeer &p) { return p.id == id; }),
							peers_.end());
				} else {
					id = next_peer_id_++;
				}
				next_peer_id_ = std::max(next_peer_id_, id + 1);
				lobby_auth_pending_.erase(id); // a check for the id's last holder
				np.id = id;
				np.address = from_ip;
				np.port = from_port;
				np.connected = true;
				np.last_seen_ms = now_ms;
				np.guid = "peer-" + std::to_string(np.id);
				peers_.push_back(np);
				peer = &peers_.back();
				sender_peer_id = peer->id;

				std::cout << "[NetHost] Accepted connection from " << from_ip << ":" << from_port << " assigned peer_id " << peer->id << "\n";

				// Send connect ACK back
				send_wire_packet(from_ip, from_port, peer->id, NET_FLAG_CONNECT_ACK, 0, nullptr, 0, false);

				// Send server handshake
				ServerHandshakeMsg shm;
				shm.engine_version = software_version_;
				std::vector<uint8_t> payload = NetPacketSerializer::frame_server_handshake(shm);
				std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_SERVER_HANDSHAKE, 0, payload);
				send_wire_packet(from_ip, from_port, peer->id, 0, cfg.commandChannel, msg.data(), msg.size(), true);
				// It joins the session (peer_connected_cb_) once it authenticates.
			}
		} else if (peer) {
			peer->last_seen_ms = now_ms;
			sender_peer_id = peer->id;
			if (hdr.flags & NET_FLAG_CONNECT_REQ) {
				send_wire_packet(from_ip, from_port, peer->id, NET_FLAG_CONNECT_ACK, 0, nullptr, 0, false);
			}
		}
		// A datagram from an address that never connected (or a full or banned
		// session) speaks for nobody: the id on its header is its own claim.
		if (!peer) return;
	} else if (isClient()) {
		if (!peers_.empty()) {
			peers_[0].last_seen_ms = now_ms;
			if (hdr.flags & NET_FLAG_CONNECT_ACK) {
				local_peer_id_ = hdr.peer_id;
				peers_[0].connected = true;
				std::cout << "[NetHost] Connected to host, assigned local peer_id " << local_peer_id_ << "\n";
				// In the session (peer_connected_cb_) once the host accepts our
				// authentication (NMT_AUTHENTICATE_RESULT).
			}
			// The host closes our link (it left, or kicked or refused us). Its
			// header carries our own id, so the generic handler below misses it.
			if (hdr.flags & NET_FLAG_DISCONNECT) {
				const bool refused = auth_result_ >= 0 && auth_result_ != ARC_OK;
				lose_host(kicked_self_ ? (banned_self_ ? 7 : 6)
						: hdr.channel ? hdr.channel // a reason the host named (NDR_*)
						: refused && auth_result_ == ARC_BANNED ? 7
						: refused && auth_result_ == ARC_PASSWORD_INVALID ? 14 : 2);
			}
		}
	}

	// Handle ACK
	if (hdr.flags & NET_FLAG_ACK) {
		for (auto it = pending_acks_.begin(); it != pending_acks_.end(); ) {
			// ACKs are per packet, not cumulative: acking seq 6 says nothing
			// about a lost seq 5, which must stay queued for retransmission.
			if (it->dest_ip == from_ip && it->dest_port == from_port && it->seq == hdr.ack) {
				uint32_t rtt = static_cast<uint32_t>(now_ms - it->sent_time_ms);
				stats_.rtt_ms = (stats_.rtt_ms == 0 ? rtt : (stats_.rtt_ms * 7 + rtt) / 8);
				NetPeer *p = getPeer(it->peer_id);
				if (p) {
					p->rtt_ms = (p->rtt_ms == 0 ? rtt : (p->rtt_ms * 7 + rtt) / 8);
				}
				it = pending_acks_.erase(it);
			} else {
				++it;
			}
		}
	}

	const uint8_t *payload_ptr = data + sizeof(NetWireHeader);
	const size_t payload_size = std::min(static_cast<size_t>(hdr.payload_len), size - sizeof(NetWireHeader));
	// If packet is reliable, send ACK back
	if (hdr.flags & NET_FLAG_RELIABLE) {
		send_ack(from_ip, from_port, hdr.peer_id, hdr.seq);
		NetPeer *p = getPeer(sender_peer_id);
		if (p) {
			// Duplicate only if this exact seq was delivered before. A max-seq
			// test would discard the retransmit of a packet lost before a later
			// one arrived, after acking it, so it would never be delivered.
			if (hdr.seq <= p->last_recv_seq || p->recv_ahead.count(hdr.seq)) {
				return;
			}
			if (hdr.seq != p->last_recv_seq + 1) {
				// Early: it waits for the ones before it (ENet's in-order channel),
				// so a pause cannot overtake its resume or a part its message.
				p->recv_ahead.insert(hdr.seq);
				p->held_packets[hdr.seq] = {hdr.flags, std::vector<uint8_t>(payload_ptr, payload_ptr + payload_size)};
				if (!p->held_since_ms) p->held_since_ms = now_ms;
				return;
			}
			++p->last_recv_seq;
			deliver_packet(hdr.flags, payload_ptr, payload_size, sender_peer_id);
			// The callbacks can drop peers; look the sender up again.
			if (NetPeer *q = getPeer(sender_peer_id)) release_held(*q, false);
			return;
		}
	}
	deliver_packet(hdr.flags, payload_ptr, payload_size, sender_peer_id);
}

void NetHost::release_held(NetPeer &p, bool skip) {
	const uint32_t id = p.id;
	while (true) {
		NetPeer *q = getPeer(id);
		if (!q || q->held_packets.empty()) {
			if (q) q->held_since_ms = 0;
			return;
		}
		auto it = q->held_packets.begin();
		if (it->first != q->last_recv_seq + 1) {
			if (!skip) {
				q->held_since_ms = get_now_ms(); // a newer gap: its own wait
				return;
			}
			std::cout << "[NetHost] Peer " << id << ": reliable seqs " << q->last_recv_seq + 1 << "-" << it->first - 1
			          << " never arrived; delivering after them\n";
			q->fragments.clear();
		}
		skip = false;
		const uint32_t seq = it->first;
		const uint8_t flags = it->second.first;
		const std::vector<uint8_t> payload = std::move(it->second.second);
		q->held_packets.erase(it);
		q->recv_ahead.erase(seq);
		q->last_recv_seq = seq;
		deliver_packet(flags, payload.data(), payload.size(), id);
	}
}

void NetHost::check_held(uint64_t now_ms) {
	for (size_t i = 0; i < peers_.size(); ++i)
		if (!peers_[i].held_packets.empty() && peers_[i].held_since_ms && now_ms - peers_[i].held_since_ms > kHoldGapMs)
			release_held(peers_[i], true);
}

void NetHost::deliver_packet(uint8_t flags, const uint8_t *payload_ptr, size_t payload_size, uint32_t sender_peer_id) {
	// ENet reassembly: the parts of one message come in order (see above).
	std::vector<uint8_t> whole;
	if (NetPeer *fp = (flags & NET_FLAG_RELIABLE) ? getPeer(sender_peer_id) : nullptr) {
		if (flags & NET_FLAG_FRAGMENT) {
			if (fp->fragments.size() + payload_size <= (16u << 20))
				fp->fragments.insert(fp->fragments.end(), payload_ptr, payload_ptr + payload_size);
			return;
		}
		if (!fp->fragments.empty()) {
			whole.swap(fp->fragments);
			whole.insert(whole.end(), payload_ptr, payload_ptr + payload_size);
			payload_ptr = whole.data();
			payload_size = whole.size();
		}
	}
	// Handle Disconnect
	if (flags & NET_FLAG_DISCONNECT) {
		NetPeer *p = getPeer(sender_peer_id);
		if (p) {
			p->connected = false;
			std::cout << "[NetHost] Peer " << sender_peer_id << " disconnected\n";
			if (peer_disconnected_cb_) {
				peer_disconnected_cb_(sender_peer_id);
			}
		}
	}

	// Process payload
	if (payload_size > 0) {
		NetMessage net_msg;
		if (NetPacketSerializer::unpack_message(payload_ptr, payload_size, net_msg)) {
			// dispatchMessage() owns message_cb_ delivery for every type (once
			// per message, with the resolved sender id). Firing it here as well
			// would hand TurnManager every NMT_DIPLOMACY / NMT_JOIN_SYNC_* packet
			// TWICE -- a duplicated stance queues twice and a duplicated resync
			// request is answered twice.
			dispatchMessage(sender_peer_id, net_msg);
		} else {
			const NetPeer *from = isHost() ? getPeer(sender_peer_id) : nullptr;
			if (command_cb_ && (!isHost() || (from && admitted(*from)))) {
				command_cb_(sender_peer_id, payload_ptr, payload_size);
			}
		}
	}
}

void NetHost::dispatchMessage(uint32_t peerId, const NetMessage &msg) {
	// The authoritative sender id, resolved once for every branch below (see
	// resolve_sender_id: a wire peer_id of 0 is ambiguous -- it is the host's own id
	// AND what an un-acked client stamps).
	const uint32_t author = resolve_sender_id(peerId);
	if (isHost() && author != 0) {
		// 0 A.D. NetServer's session states: a client that has not authenticated
		// (or was kicked) may only hand-shake; nothing else it says is heard.
		const NetPeer *p = getPeer(author);
		const bool hello = msg.type == static_cast<uint8_t>(NetMessageType::NMT_CLIENT_HANDSHAKE) ||
		                   msg.type == static_cast<uint8_t>(NetMessageType::NMT_AUTHENTICATE);
		if (!p || (!admitted(*p) && !(hello && p->connected && !p->kick_disconnect_ms))) return;
	}
	if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SIMULATION_COMMAND)) {
		if (command_cb_) {
			command_cb_(peerId, msg.payload.data(), msg.payload.size());
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_CHAT)) {
		ChatMsg chat;
		if (NetPacketSerializer::parse_chat(msg.payload.data(), msg.payload.size(), chat)) {
			if (isHost()) route_chat(std::move(chat), author);
			else if (chat_cb_) chat_cb_(chat); // the host already stamped and routed it
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_CLIENT_PERFORMANCE)) {
		ClientPerformanceMsg m;
		if (isClient() && author == 0 &&
				NetPacketSerializer::parse_client_performance(msg.payload.data(), msg.payload.size(), m)) {
			client_perf_ = std::move(m.clients);
			++client_perf_reports_;
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_PLAYER_ASSIGNMENT)) {
		PlayerAssignmentMsg m;
		if (isClient() && author == 0 &&
				NetPacketSerializer::parse_player_assignment(msg.payload.data(), msg.payload.size(), m)) {
			player_assignment_ = std::move(m.clients);
			++player_assignments_;
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY)) {
		// Retired: a stance is lockstep cmd 22 now (see net_types.h). Dropped
		// here and never relayed, so an old or hostile peer cannot change
		// anyone's stances out of band.
		std::cerr << "[NetHost] dropped a retired NMT_DIPLOMACY packet; stances travel as cmd 22\n";
		return;
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_STATE)) {
		// Late-join turn-0 state resync. Only a HOST answers, and only for an
		// admitted peer: the attributes it replays are the ones it already
		// broadcast at Start, so a stranger dialing in can never make the host
		// reset its world (0 A.D. NetServer.cpp:1366 sends to a known session).
		JoinSyncMsg jsm;
		if (!NetPacketSerializer::parse_join_sync(msg.payload.data(), msg.payload.size(), jsm)) {
			std::cerr << "[NetHost] NMT_JOIN_SYNC_STATE payload must be exactly 5 bytes (role, requested_turn)\n";
		} else if (jsm.role != JoinSyncMsg::ROLE_JOINER) {
			// Host-side replies are consumed by TurnManager::receive_join_sync.
		} else if (!isHost()) {
			// A client ignores a join request from anyone but its host.
		} else {
			const NetPeer *requester = getPeer(author);
			uint8_t err = TurnStateSnapshotMsg::ERR_UNKNOWN;
			bool accepted = false;
			if (!requester || !requester->connected) {
				err = TurnStateSnapshotMsg::ERR_PEER_ADMITTED;
			} else if (!game_start_sent_) {
				err = TurnStateSnapshotMsg::ERR_NOT_AUTHORITATIVE;
			} else {
				accepted = true;
			}
			if (accepted) {
				// The owner decides whether it can actually serve the request (it
				// may already have left the turn-0 sync point) and replies with the
				// state the joiner can rebuild. Ordering is not relied upon: the
				// NMT_GAME_START resend and this reply are independent reliable
				// messages and the joiner tolerates either order.
				uint32_t reply_turn = jsm.requested_turn;
				uint32_t reply_hash = 0;
				bool answered = join_sync_request_cb_ &&
				                join_sync_request_cb_(author, jsm.requested_turn, reply_turn, reply_hash);
				if (!answered) err = TurnStateSnapshotMsg::ERR_JOINED_LATE;
				std::vector<uint8_t> pkt = join_sync_reply_packet(
					JoinSyncMsg::ROLE_HOST, reply_turn, answered ? 1 : 0, reply_hash, answered ? 0 : err);
				sendCommand(peerId, pkt.data(), pkt.size());
			} else {
				std::vector<uint8_t> pkt = join_sync_reply_packet(
					JoinSyncMsg::ROLE_HOST, jsm.requested_turn, 0, 0, err);
				sendCommand(peerId, pkt.data(), pkt.size());
			}
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_READY)) {
		// 0 A.D. NetServer OnReady: the host stamps the sender, keeps its status
		// and tells the others; a ready that comes once loading began is
		// dropped. Clients read everyone's status from NMT_GAME_SETUP.
		ReadyMsg rdy;
		NetPeer *p = getPeer(author);
		if (isHost() && p && admitted(*p) && !game_start_sent_ &&
				NetPacketSerializer::parse_ready(msg.payload.data(), msg.payload.size(), rdy)) {
			p->ready_status = rdy.status <= 2 ? rdy.status : 1;
			p->ready_version = rdy.setup_version;
			rdy.guid = peer_guid(author);
			std::vector<uint8_t> payload = NetPacketSerializer::frame_ready(rdy);
			std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_READY, 0, payload);
			broadcastCommand(pkt.data(), pkt.size(), author);
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_GAME_SETUP)) {
		// 0 A.D. NetClient OnGameSetup: only the host's counts.
		GameStartMsg m;
		if (isClient() && author == 0 && NetPacketSerializer::parse_game_start(msg.payload.data(), msg.payload.size(), m)) {
			if (game_setups_.size() >= 64) game_setups_.erase(game_setups_.begin());
			game_setups_.push_back(std::move(m.init_attributes));
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SETUP_REQUEST)) {
		GameStartMsg m;
		const NetPeer *p = getPeer(author);
		if (isHost() && p && admitted(*p) && !game_start_sent_ && setup_requests_.size() < 64 &&
				NetPacketSerializer::parse_game_start(msg.payload.data(), msg.payload.size(), m) &&
				m.init_attributes.size() <= 64)
			setup_requests_.push_back({author, std::move(m.init_attributes)});
		// The host's answer to one of ours: why it was turned down.
		else if (isClient() && author == 0 && setup_refusals_.size() < 64 &&
				NetPacketSerializer::parse_game_start(msg.payload.data(), msg.payload.size(), m) &&
				m.init_attributes.rfind("refused=", 0) == 0)
			setup_refusals_.push_back(m.init_attributes.substr(8));
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_AUTHENTICATE)) {
		AuthenticateMsg auth;
		if (NetPacketSerializer::parse_authenticate(msg.payload.data(), msg.payload.size(), auth)) {
			NetPeer *p = getPeer(peerId);
			// 0 A.D. OnAuthenticate: "Prohibit joins while the game is loading".
			if (isHost() && p && !p->is_authenticated && !p->kick_disconnect_ms && server_loading_) {
				std::cout << "[NetHost] Refused peer " << p->id << " while the game is loading\n";
				refuse_with(*p, NDR_SERVER_LOADING);
			} else if (isHost() && p && !p->is_authenticated && !p->kick_disconnect_ms) {
				const std::string sent = auth.name;
				auth.name = sanitise_player_name(auth.name);
				auth.sent_name = sent; // the password's salt
				// 0 A.D. NSS_LOBBY_AUTHENTICATE: a lobby host first asks the
				// lobby whose ticket this is (one check per joiner at a time).
				if (!lobby_auth_) {
					authenticate_peer(*p, std::move(auth));
				} else if (!lobby_auth_pending_.count(p->id)) {
					const uint32_t id = p->id;
					const std::string ticket = auth.lobby_ticket;
					lobby_auth_pending_[id] = std::move(auth);
					lobby_auth_(id, ticket);
				}
			}
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_AUTHENTICATE_RESULT)) {
		AuthenticateResultMsg res;
		if (isClient() && author == 0 &&
				NetPacketSerializer::parse_authenticate_result(msg.payload.data(), msg.payload.size(), res) &&
				auth_result_ < 0) {
			auth_result_ = (int)res.code;
			std::cout << "[NetHost] Host answered: " << res.message << "\n";
			if (res.code == ARC_OK && peer_connected_cb_) peer_connected_cb_(0);
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_KICKED)) {
		KickedMsg k;
		if (isClient() && author == 0 && NetPacketSerializer::parse_kicked(msg.payload.data(), msg.payload.size(), k)) {
			if (k.guid == peer_guid(local_peer_id_)) {
				kicked_self_ = true;
				banned_self_ = k.ban != 0;
			}
			std::cout << "[NetHost] " << k.name << (k.ban ? " was banned" : " was kicked") << "\n";
			kicks_.push_back(std::move(k));
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_CLIENT_HANDSHAKE)) {
		ClientHandshakeMsg chm;
		if (NetPacketSerializer::parse_client_handshake(msg.payload.data(), msg.payload.size(), chm)) {
			NetPeer *p = isHost() ? getPeer(author) : nullptr;
			// 0 A.D. CNetServerWorker::OnClientHandshake: another protocol, or
			// another engine version or mods (CheckHandshake), closes the link.
			if (p && !p->kick_disconnect_ms &&
					(chm.protocol_version != PS_PROTOCOL_VERSION || chm.engine_version != software_version_ ||
						chm.enabled_mods != enabled_mods_)) {
				std::cout << "[NetHost] Refused peer " << peerId << ": it runs " << chm.engine_version << "\n";
				refuse_with(*p, chm.protocol_version != PS_PROTOCOL_VERSION ? NDR_INCORRECT_PROTOCOL_VERSION
						: NDR_INCORRECT_SOFTWARE_VERSION);
				return;
			} else if (p && !p->kick_disconnect_ms) {
				ServerHandshakeResponseMsg srm;
				srm.use_protocol_version = PS_PROTOCOL_VERSION;
				srm.flags = lobby_auth_ ? PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH : 0;
				srm.guid = "peer-" + std::to_string(peerId);
				std::vector<uint8_t> payload = NetPacketSerializer::frame_server_handshake_response(srm);
				std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SERVER_HANDSHAKE_RESPONSE, 0, payload);
				sendCommand(peerId, pkt.data(), pkt.size());
				if (!lobby_rooms_.empty()) {
					lobby_rooms_[0].players = 1 + static_cast<int>(peers_.size());
				}
			}
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SERVER_HANDSHAKE)) {
		// The host's engine version, for 0.28's mismatch detail (network.js
		// getMismatchMessage) should the host refuse this one.
		ServerHandshakeMsg shm;
		if (isClient() && NetPacketSerializer::parse_server_handshake(msg.payload.data(), msg.payload.size(), shm))
			host_software_version_ = shm.engine_version;
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SERVER_HANDSHAKE_RESPONSE)) {
		if (isClient()) {
			if (!peers_.empty()) peers_[0].connected = true;
			NetPeer *p = getPeer(peerId);
			if (p) p->connected = true;
			// 0 A.D. NetClient OnHandshakeResponse: a host that checks names
			// with the lobby needs this join to come through the lobby.
			ServerHandshakeResponseMsg srm;
			if (lobby_ticket_.empty() && auth_result_ < 0 &&
					NetPacketSerializer::parse_server_handshake_response(msg.payload.data(), msg.payload.size(), srm) &&
					(srm.flags & PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH)) {
				std::cout << "[NetHost] The host wants a lobby join; no lobby ticket\n";
				lose_host(NDR_LOBBY_AUTH_FAILED);
				send_wire_packet(server_ip_, server_port_, 0, NET_FLAG_DISCONNECT, 0, nullptr, 0, false);
				return;
			}
			sendAuthenticate(local_player_name_, password_);
		}
	}
	// Single delivery point for every message type, exactly once, with the
	// resolved sender id: SYNC_CHECK / GAME_START / END_COMMAND_BATCH /
	// NMT_JOIN_SYNC_* and any future type land in
	// TurnManager::onMessage, which owns them. dispatchMessage() is also the
	// public entry point tests use, so this keeps direct calls and the socket path
	// behaving identically.
	if (message_cb_) message_cb_(author, msg);
}

// Reliable means delivered or the link is gone, as ENet's reliable packets
// are: resend until the peer acks, backing off from 200 ms to 1 s, and give
// up only once the peer has dropped or timed out (nothing heard for 10 s).
// Giving up sooner lost a turn batch or a rejoin chunk for good whenever a
// peer's frames stalled for a couple of seconds, and the match or the rejoin
// froze; giving up 10 s after the first send did the same on a lossy link
// whose peer was still talking (its heartbeats kept arriving).
void NetHost::check_retransmissions(uint64_t now_ms) {
	for (auto it = pending_acks_.begin(); it != pending_acks_.end(); ) {
		const NetPeer *p = getPeer(it->peer_id);
		// (A client's link to the host reads unconnected until the host acks it.)
		const bool silent = !p || now_ms - p->last_seen_ms > 10000;
		const bool gone = !p || (isHost() && !p->connected) ||
				(now_ms - it->first_sent_ms > 10000 && silent);
		const uint64_t wait = std::min<uint64_t>(200ull << std::min(it->retry_count, 3), 1000);
		if (gone) {
			stats_.dropped_packets++;
			it = pending_acks_.erase(it);
		} else if (now_ms - it->sent_time_ms > wait) {
			send_raw_packet(it->dest_ip, it->dest_port, it->packet_data.data(), it->packet_data.size());
			it->sent_time_ms = now_ms;
			it->retry_count++;
			++it;
		} else {
			++it;
		}
	}
}

void NetHost::send_heartbeats(uint64_t now_ms) {
	if (now_ms - last_heartbeat_ms_ < 1000) return;
	last_heartbeat_ms_ = now_ms;

	if (isClient() && !server_ip_.empty()) {
		send_wire_packet(server_ip_, server_port_, 0, NET_FLAG_HEARTBEAT, 0, nullptr, 0, false);
	} else if (isHost()) {
		for (const auto &p : peers_) {
			if (p.connected) {
				send_wire_packet(p.address, p.port, p.id, NET_FLAG_HEARTBEAT, 0, nullptr, 0, false);
			}
		}
	}
}

void NetHost::check_peer_timeouts(uint64_t now_ms) {
	for (auto &p : peers_) {
		// An ENet connect nobody answers times out with reason 0 (0.28
		// reportConnectionRequestTimeOut); the connect request itself stops
		// resending after 10 s, so waiting longer would wait forever.
		if (isClient() && p.id == 0 && !p.connected && disconnect_reason_ < 0 && auth_result_ < 0 &&
				p.last_seen_ms > 0 && now_ms - p.last_seen_ms > kConnectTimeoutMs) {
			std::cout << "[NetHost] The connection request to " << server_ip_ << ":" << server_port_ << " timed out\n";
			lose_host(0);
			continue;
		}
		// enet_peer_disconnect_later: the refusal or kick notice goes out
		// reliably first. Closing the link at the fixed 2 s dropped it whenever
		// loss outlasted that, and the client waited without a reason.
		if (p.connected && p.kick_disconnect_ms && now_ms >= p.kick_disconnect_ms &&
				now_ms < p.kick_disconnect_ms + 8000 &&
				std::any_of(pending_acks_.begin(), pending_acks_.end(),
						[&](const auto &a) { return a.peer_id == p.id; }))
			continue;
		if (p.connected && p.kick_disconnect_ms && now_ms >= p.kick_disconnect_ms) {
			for (int i = 0; i < kDisconnectSends; ++i)
				send_wire_packet(p.address, p.port, p.id, NET_FLAG_DISCONNECT, p.disconnect_reason, nullptr, 0, false);
			p.connected = false;
			continue;
		}
		if (p.connected && p.last_seen_ms > 0 && (now_ms - p.last_seen_ms > 10000)) {
			if (isClient() && p.id == 0) {
				// ENet's timeout disconnect carries no reason (NDR_UNKNOWN),
				// unless the host already said it kicked us.
				lose_host(kicked_self_ ? (banned_self_ ? 7 : 6) : 0);
				continue;
			}
			p.connected = false;
			std::cout << "[NetHost] Peer " << p.id << " timed out\n";
			if (peer_disconnected_cb_) {
				peer_disconnected_cb_(p.id);
			}
		}
	}
}

void NetHost::poll(Arena &arena) {
	(void)arena;
	if (socket_fd_ < 0) return;

	// A whole UDP datagram: a shorter buffer cuts a long one off unseen.
	static thread_local std::vector<uint8_t> recv_buf(65536);
	uint8_t *buffer = recv_buf.data();
	sockaddr_in from_addr{};

	while (true) {
		socklen_t from_len = sizeof(from_addr);
		ssize_t bytes = recvfrom(static_cast<SOCKET>(socket_fd_), reinterpret_cast<char*>(buffer), recv_buf.size(), 0, reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
		if (bytes <= 0) {
			break;
		}
		char ip_str[INET_ADDRSTRLEN] = {0};
		inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
		uint16_t from_port = ntohs(from_addr.sin_port);
		handle_incoming_packet(buffer, static_cast<size_t>(bytes), ip_str, from_port);
	}

	uint64_t now = get_now_ms();
	check_stun(now);
	check_held(now);
	send_hole_punches(now);
	check_retransmissions(now);
	send_heartbeats(now);
	check_peer_timeouts(now);
	send_client_performance(now);
}

void NetHost::set_simulated_firewall(bool on) {
	sim_firewall_ = on;
	sent_to_.clear();
	// What this socket already sent to (a client's connect request) is open.
	if (on)
		for (const auto &p : peers_) sent_to_.insert(p.address + ":" + std::to_string(p.port));
}

void NetHost::queryStun(const std::string &server, uint16_t port) {
	if (socket_fd_ < 0) return;
	stun_state_ = 3;
	public_ip_.clear();
	public_port_ = 0;
	lan_ip_ = localIp();
	// Resolved once: send_raw_packet would look the name up on every resend.
	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo *res = nullptr;
	if (getaddrinfo(server.c_str(), nullptr, &hints, &res) != 0 || !res) {
		std::cerr << "[NetHost] STUN: cannot resolve " << server << "\n";
		return;
	}
	char ip[INET_ADDRSTRLEN] = {0};
	inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr, ip, sizeof(ip));
	freeaddrinfo(res);
	stun_server_ip_ = ip;
	stun_server_port_ = port ? port : stun::DEFAULT_PORT;
	// The transaction id only has to tell this answer from a stale one.
	uint64_t x = get_now_ms() ^ ((uint64_t)(uintptr_t)this << 16) ^ ((uint64_t)localPort() << 40);
	for (int i = 0; i < 12; ++i) {
		x ^= x << 13; x ^= x >> 7; x ^= x << 17;
		stun_txn_[i] = (uint8_t)x;
	}
	stun_state_ = 1;
	stun_started_ms_ = get_now_ms();
	stun_sent_ms_ = 0;
	check_stun(stun_started_ms_);
}

void NetHost::check_stun(uint64_t now_ms) {
	if (stun_state_ != 1) return;
	if (now_ms - stun_started_ms_ > STUN_TIMEOUT_MS) {
		stun_state_ = 3;
		std::cerr << "[NetHost] STUN: no answer from " << stun_server_ip_ << ":" << stun_server_port_ << "\n";
		return;
	}
	if (stun_sent_ms_ && now_ms - stun_sent_ms_ < STUN_RESEND_MS) return;
	stun_sent_ms_ = now_ms;
	const std::vector<uint8_t> req = stun::binding_request(stun_txn_);
	send_raw_packet(stun_server_ip_, stun_server_port_, req.data(), req.size());
}

void NetHost::holePunch(const std::string &ip, uint16_t port) {
	if (socket_fd_ < 0 || ip.empty() || port == 0) return;
	HolePunch h;
	h.ip = ip;
	h.port = port;
	h.left = HOLE_PUNCH_SENDS;
	hole_punches_.push_back(h);
	send_hole_punches(get_now_ms());
}

void NetHost::send_hole_punches(uint64_t now_ms) {
	for (auto it = hole_punches_.begin(); it != hole_punches_.end();) {
		if (now_ms >= it->next_ms) {
			send_raw_packet(it->ip, it->port, stun::HOLE_PUNCH, sizeof(stun::HOLE_PUNCH));
			it->next_ms = now_ms + HOLE_PUNCH_DELAY_MS;
			if (--it->left <= 0) {
				it = hole_punches_.erase(it);
				continue;
			}
		}
		++it;
	}
}

uint32_t NetHost::hostSilentMs() const {
	if (!isClient() || peers_.empty() || !peers_[0].last_seen_ms) return 0;
	const uint64_t now = get_now_ms();
	return now > peers_[0].last_seen_ms ? (uint32_t)std::min<uint64_t>(now - peers_[0].last_seen_ms, 0xFFFFFFFFu) : 0;
}

uint16_t NetHost::localPort() const {
	if (socket_fd_ < 0) return 0;
	sockaddr_in a{};
	socklen_t len = sizeof(a);
	if (getsockname(static_cast<SOCKET>(socket_fd_), reinterpret_cast<sockaddr *>(&a), &len) != 0) return 0;
	return ntohs(a.sin_port);
}

std::string NetHost::localIp() {
	ensure_socket_subsystem();
	// A connected UDP socket sends nothing; the kernel just picks the
	// interface that routes to the internet (as StunClient::FindLocalIP).
	SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!IS_SOCKET_VALID(fd)) return "";
	sockaddr_in dst{};
	dst.sin_family = AF_INET;
	dst.sin_port = htons(53);
	inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
	std::string ip;
	if (connect(fd, reinterpret_cast<sockaddr *>(&dst), sizeof(dst)) == 0) {
		sockaddr_in a{};
		socklen_t len = sizeof(a);
		char buf[INET_ADDRSTRLEN] = {0};
		if (getsockname(fd, reinterpret_cast<sockaddr *>(&a), &len) == 0 &&
				inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf)))
			ip = buf;
	}
	CLOSE_SOCKET(fd);
	return ip == "0.0.0.0" ? "" : ip;
}

void NetHost::retargetHost(const std::string &ip, uint16_t port) {
	if (!isClient() || peers_.empty() || ip.empty() || port == 0) return;
	for (auto &pk : pending_acks_) {
		if (pk.dest_ip == server_ip_ && pk.dest_port == server_port_) {
			pk.dest_ip = ip;
			pk.dest_port = port;
			pk.sent_time_ms = 0; // resend now, to the new address
			pk.first_sent_ms = get_now_ms();
		}
	}
	peers_[0].last_seen_ms = get_now_ms();
	server_ip_ = ip;
	server_port_ = port;
	peers_[0].address = ip;
	peers_[0].port = port;
	std::cout << "[NetHost] Client connecting to " << ip << ":" << port << " instead\n";
}

// 0 A.D. CNetServerWorker::OnAuthenticate, past the lobby check.
void NetHost::authenticate_peer(NetPeer &peer, AuthenticateMsg auth) {
	NetPeer *p = &peer;
	const uint32_t peerId = p->id;
	// A lobby check that ends after Start (0.28 StartGame drops a client not
	// yet past authentication with NDR_SERVER_LOADING).
	if (server_loading_) {
		std::cout << "[NetHost] Refused peer " << peerId << " while the game is loading\n";
		refuse_with(*p, NDR_SERVER_LOADING);
		return;
	}
	const std::string sent_name = auth.sent_name.empty() ? auth.name : auth.sent_name; // the password's salt, as sent
	// 0 A.D. NetServer::OnAuthenticate: a name already in the game is
	// refused, or with network.duplicateplayernames made unique
	// (DeduplicatePlayerName); unnamed (test) clients are left alone.
	if (!auth.name.empty() && name_taken(auth.name, p)) {
		if (!duplicate_names_) {
			std::cout << "[NetHost] Refused peer " << p->id << ": the name " << auth.name << " is in use\n";
			refuse_with(*p, NDR_PLAYERNAME_IN_USE);
			return;
		}
		std::string name;
		for (int i = 2; name_taken(name = auth.name + " (" + std::to_string(i) + ")", p); ++i) {}
		auth.name = name;
	}
	// A banned name or address, or the wrong password, is refused and
	// the link closed.
	AuthenticateResultMsg res;
	res.host_id = static_cast<uint16_t>(peerId);
	res.is_controller = 0;
	int &failed = failed_passwords_[auth.name.empty() ? p->address : auth.name];
	// NetServer: behind the lobby a name is banned without its rating ("name (1500)").
	if (banned_names_.count(lobbyAuth() ? auth.name.substr(0, auth.name.find(" (")) : auth.name) ||
			banned_addresses_.count(p->address) ||
			failed >= FAILED_PASSWORD_TRIES_BEFORE_BAN) {
		res.code = ARC_BANNED;
		res.message = "You have been banned from this game";
	} else if (!password_.empty() &&
			auth.password != (password_hasher_ ? password_hasher_(password_, sent_name) : password_)) {
		++failed;
		res.code = ARC_PASSWORD_INVALID;
		res.message = "Wrong password";
	} else {
		failed = 0;
		res.code = ARC_OK;
		res.message = "Welcome " + auth.name;
	}
	std::vector<uint8_t> payload = NetPacketSerializer::frame_authenticate_result(res);
	std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_AUTHENTICATE_RESULT, 0, payload);
	sendCommand(peerId, pkt.data(), pkt.size());
	if (res.code == ARC_OK) {
		p->player_name = auth.name;
		p->is_authenticated = true;
		if (peer_connected_cb_) peer_connected_cb_(p->id);
	} else {
		std::cout << "[NetHost] Refused peer " << p->id << " (" << auth.name << "): " << res.message << "\n";
		p->kick_disconnect_ms = get_now_ms() + 2000; // the answer gets there first
	}
}

// 0 A.D. CNetServerWorker::SanitisePlayerName: no GUI tags ("[" and "]" become
// "{" and "}"), at most 32 characters, no surrounding white space, and
// "Anonymous" for a name of only white space. RTS0's unnamed test clients
// (an empty name) stay unnamed.
std::string NetHost::sanitise_player_name(const std::string &original) {
	if (original.empty()) return original;
	std::string name = original;
	for (char &c : name) {
		if (c == '[') c = '{';
		else if (c == ']') c = '}';
	}
	size_t chars = 0, cut = name.size();
	for (size_t i = 0; i < name.size(); ++i) {
		if ((static_cast<unsigned char>(name[i]) & 0xC0) == 0x80) continue; // inside a character
		if (chars++ == 32) { cut = i; break; }
	}
	name.resize(cut);
	const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
	while (!name.empty() && space(name.back())) name.pop_back();
	size_t lead = 0;
	while (lead < name.size() && space(name[lead])) ++lead;
	name.erase(0, lead);
	return name.empty() ? "Anonymous" : name;
}

bool NetHost::finishLobbyAuth(uint32_t peer_id, const std::string &ticket, const std::string &lobby_name) {
	auto it = lobby_auth_pending_.find(peer_id);
	if (it == lobby_auth_pending_.end() || it->second.lobby_ticket != ticket) return false;
	AuthenticateMsg auth = std::move(it->second);
	lobby_auth_pending_.erase(it);
	NetPeer *p = isHost() ? getPeer(peer_id) : nullptr;
	if (!p || p->is_authenticated || p->kick_disconnect_ms) return true;
	// "[...] comparisons will be made in case-normalized canonical form."
	// (XEP-0029), on the name without its lobby rating.
	auto lower = [](std::string v) {
		for (char &c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return v;
	};
	const std::string sent = auth.name.substr(0, auth.name.find(" ("));
	if (lobby_name.empty() || lower(sent) != lower(lobby_name)) {
		std::cout << "[NetHost] Lobby auth: " << (lobby_name.empty() ? "an unknown ticket" : lobby_name)
				  << " tried joining as " << sent << "\n";
		refuse_with(*p, NDR_LOBBY_AUTH_FAILED);
		return true;
	}
	authenticate_peer(*p, std::move(auth));
	return true;
}

void NetHost::lose_host(int reason) {
	if (disconnect_reason_ >= 0 || peers_.empty()) return;
	disconnect_reason_ = reason;
	peers_[0].connected = false;
	std::cout << "[NetHost] Lost the connection to the host (reason " << reason << ")\n";
	if (peer_disconnected_cb_) peer_disconnected_cb_(0);
}

void NetHost::refuse_with(NetPeer &p, uint8_t reason) {
	p.disconnect_reason = reason;
	p.kick_disconnect_ms = get_now_ms();
}

bool NetHost::name_taken(const std::string &name, const NetPeer *self) const {
	if (name == local_player_name_) return true;
	for (const auto &p : peers_)
		if (&p != self && p.id != 0 && admitted(p) && p.player_name == name) return true;
	return false;
}

void NetHost::refusePeer(uint32_t peer_id, uint8_t reason) {
	NetPeer *p = isHost() && peer_id != 0 ? getPeer(peer_id) : nullptr;
	if (p && p->connected && !p->kick_disconnect_ms) {
		p->kick_disconnect_ms = get_now_ms() + 2000;
		if (reason) p->disconnect_reason = reason;
	}
}

bool NetHost::kickPeer(uint32_t peer_id, bool ban) {
	NetPeer *p = isHost() && peer_id != 0 ? getPeer(peer_id) : nullptr;
	if (!p || !admitted(*p)) return false;
	KickedMsg k;
	k.guid = peer_guid(p->id);
	k.name = p->player_name;
	k.ban = ban ? 1 : 0;
	kicks_.push_back(k);
	if (ban) {
		if (!p->player_name.empty())
			banned_names_.insert(lobbyAuth() ? p->player_name.substr(0, p->player_name.find(" (")) : p->player_name);
		banned_addresses_.insert(p->address);
	}
	// Everyone hears it, the kicked client included (reliable, before the link closes).
	std::vector<uint8_t> payload = NetPacketSerializer::frame_kicked(k);
	broadcastCommand(NetPacketSerializer::pack_message(NetMessageType::NMT_KICKED, 0, payload));
	p->kick_disconnect_ms = get_now_ms() + 2000;
	std::cout << "[NetHost] " << k.name << " (peer " << p->id << ")" << (ban ? " banned" : " kicked") << "\n";
	if (peer_disconnected_cb_) peer_disconnected_cb_(p->id); // the lockstep drops it now
	return true;
}

uint32_t NetHost::peerByName(const std::string &name) const {
	for (const auto &p : peers_)
		if (p.id != 0 && admitted(p) && p.player_name == name) return p.id;
	return 0;
}

void NetHost::sendPlayerAssignment(const PlayerAssignmentMsg &m) {
	if (!isHost()) return;
	player_assignment_ = m.clients;
	++player_assignments_;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_player_assignment(m);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_PLAYER_ASSIGNMENT, 0, payload);
	broadcastCommand(msg);
}

void NetHost::setPeerObserver(uint32_t peer_id, bool observer) {
	if (NetPeer *p = getPeer(peer_id)) p->is_observer = observer;
}

void NetHost::send_client_performance(uint64_t now_ms) {
	if (!isHost() || now_ms - last_perf_ms_ < 1000) return;
	last_perf_ms_ = now_ms;
	ClientPerformanceMsg m;
	for (const auto &p : peers_) {
		if (!admitted(p) || p.id == 0) continue;
		ClientPerformanceMsg::Entry e;
		e.client = p.id;
		e.rtt_ms = p.rtt_ms;
		e.silent_ms = p.last_seen_ms && now_ms > p.last_seen_ms ? (uint32_t)std::min<uint64_t>(now_ms - p.last_seen_ms, 0xFFFFFFFFu) : 0;
		m.clients.push_back(e);
	}
	client_perf_ = m.clients;
	++client_perf_reports_;
	if (m.clients.empty()) return;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_client_performance(m);
	// Unreliable, like 0 A.D.'s: the next second's report replaces a lost one.
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_CLIENT_PERFORMANCE, 0, payload);
	for (const auto &p : peers_)
		if (admitted(p) && p.id != 0)
			send_wire_packet(p.address, p.port, p.id, 0, cfg.commandChannel, msg.data(), msg.size(), false);
}

void NetHost::stop() {
	end_keepalive();
	if (socket_fd_ >= 0) {
		for (int i = 0; i < kDisconnectSends; ++i) {
			if (isClient() && !server_ip_.empty()) {
				send_wire_packet(server_ip_, server_port_, 0, NET_FLAG_DISCONNECT, 0, nullptr, 0, false);
			} else if (isHost()) {
				for (const auto &p : peers_) {
					if (p.connected) {
						send_wire_packet(p.address, p.port, p.id, NET_FLAG_DISCONNECT, 0, nullptr, 0, false);
					}
				}
			}
		}
		CLOSE_SOCKET(static_cast<SOCKET>(socket_fd_));
		socket_fd_ = -1;
	}
	role = NetRole::None;
	peers_.clear();
	pending_acks_.clear();
	sent_to_.clear();
	stun_state_ = 0;
	public_ip_.clear();
	public_port_ = 0;
	hole_punches_.clear();
	server_ip_.clear();
	server_port_ = 0;
	local_peer_id_ = 0;
	game_start_sent_ = false;
	game_setups_.clear();
	setup_requests_.clear();
	setup_refusals_.clear();
}

} // namespace rtso

