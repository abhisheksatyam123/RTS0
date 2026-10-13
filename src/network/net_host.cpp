#include "net_host.h"
#include <chrono>
#include <iostream>
#include <cstring>
#include <algorithm>

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

bool NetHost::startHost(const NetConfig &c) {
	stop();
	ensure_socket_subsystem();
	cfg = c;

	SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!IS_SOCKET_VALID(fd)) {
		std::cerr << "[NetHost] Failed to create UDP socket for host\n";
		return false;
	}

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
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
	kicks_.clear();
	next_seq_ = 1;
	peers_.clear();
	pending_acks_.clear();
	last_heartbeat_ms_ = get_now_ms();
	game_start_sent_ = false;

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

bool NetHost::startClient(const std::string &h, uint16_t p) {
	stop();
	ensure_socket_subsystem();
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
	next_seq_ = 1;
	peers_.clear();
	pending_acks_.clear();
	last_heartbeat_ms_ = get_now_ms();
	game_start_sent_ = false;

	NetPeer host_peer;
	host_peer.id = 0;
	host_peer.address = h;
	host_peer.port = p;
	host_peer.connected = false;
	host_peer.last_seen_ms = get_now_ms();
	peers_.push_back(host_peer);

	// Send initial connection request with client handshake
	ClientHandshakeMsg chm;
	chm.engine_version = "RTS0-0.1";
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
	am.password = password;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_authenticate(am);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_AUTHENTICATE, 0, payload);
	broadcastCommand(msg.data(), msg.size());
}

void NetHost::sendReady(bool ready) {
	ReadyMsg rm;
	rm.guid = "peer-" + std::to_string(local_peer_id_);
	rm.status = ready ? 1 : 0;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_ready(rm);
	std::vector<uint8_t> msg = NetPacketSerializer::pack_message(NetMessageType::NMT_READY, 0, payload);
	broadcastCommand(msg.data(), msg.size());
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
		NetPeer *peer = nullptr;
		for (auto &p : peers_) {
			if (p.address == from_ip && p.port == from_port) {
				peer = &p;
				break;
			}
		}

		if (!peer && (hdr.flags & NET_FLAG_CONNECT_REQ)) {
			if (peers_.size() < static_cast<size_t>(cfg.maxPlayers + cfg.maxObservers)) {
				NetPeer np;
				np.id = next_peer_id_++;
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
				shm.engine_version = "RTS0-0.1";
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
			p->recv_ahead.insert(hdr.seq);
			while (!p->recv_ahead.empty() && *p->recv_ahead.begin() == p->last_recv_seq + 1) {
				p->recv_ahead.erase(p->recv_ahead.begin());
				++p->last_recv_seq;
			}
		}
	}

	// Handle Disconnect
	if (hdr.flags & NET_FLAG_DISCONNECT) {
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
	if (hdr.payload_len > 0) {
		const uint8_t *payload_ptr = data + sizeof(NetWireHeader);
		size_t payload_size = std::min(static_cast<size_t>(hdr.payload_len), size - sizeof(NetWireHeader));

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
		ReadyMsg rdy;
		if (NetPacketSerializer::parse_ready(msg.payload.data(), msg.payload.size(), rdy)) {
			NetPeer *p = getPeer(peerId);
			if (p) {
				p->is_ready = (rdy.status != 0);
			}
			if (isHost()) {
				// Relay ready state
				std::vector<uint8_t> payload = NetPacketSerializer::frame_ready(rdy);
				std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_READY, 0, payload);
				broadcastCommand(pkt.data(), pkt.size(), peerId);
			}
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_AUTHENTICATE)) {
		AuthenticateMsg auth;
		if (NetPacketSerializer::parse_authenticate(msg.payload.data(), msg.payload.size(), auth)) {
			NetPeer *p = getPeer(peerId);
			if (isHost() && p && !p->is_authenticated) {
				// 0 A.D. NetServer::OnAuthenticate: a banned name or address, or the
				// wrong password, is refused and the link closed.
				AuthenticateResultMsg res;
				res.host_id = static_cast<uint16_t>(peerId);
				res.is_controller = 0;
				if (banned_names_.count(auth.name) || banned_addresses_.count(p->address)) {
					res.code = ARC_BANNED;
					res.message = "You have been banned from this game";
				} else if (!password_.empty() && auth.password != password_) {
					res.code = ARC_PASSWORD_INVALID;
					res.message = "Wrong password";
				} else {
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
			if (isHost()) {
				ServerHandshakeResponseMsg srm;
				srm.use_protocol_version = PS_PROTOCOL_VERSION;
				srm.flags = 0;
				srm.guid = "peer-" + std::to_string(peerId);
				std::vector<uint8_t> payload = NetPacketSerializer::frame_server_handshake_response(srm);
				std::vector<uint8_t> pkt = NetPacketSerializer::pack_message(NetMessageType::NMT_SERVER_HANDSHAKE_RESPONSE, 0, payload);
				sendCommand(peerId, pkt.data(), pkt.size());
				if (!lobby_rooms_.empty()) {
					lobby_rooms_[0].players = 1 + static_cast<int>(peers_.size());
				}
			}
		}
	} else if (msg.type == static_cast<uint8_t>(NetMessageType::NMT_SERVER_HANDSHAKE_RESPONSE)) {
		if (isClient()) {
			if (!peers_.empty()) peers_[0].connected = true;
			NetPeer *p = getPeer(peerId);
			if (p) p->connected = true;
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
// up only once the peer has dropped or would have timed out (10 s). Giving
// up sooner lost a turn batch or a rejoin chunk for good whenever a peer's
// frames stalled for a couple of seconds, and the match or the rejoin froze.
void NetHost::check_retransmissions(uint64_t now_ms) {
	for (auto it = pending_acks_.begin(); it != pending_acks_.end(); ) {
		const NetPeer *p = getPeer(it->peer_id);
		// (A client's link to the host reads unconnected until the host acks it.)
		const bool gone = !p || (isHost() && !p->connected) || now_ms - it->first_sent_ms > 10000;
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
		if (p.connected && p.kick_disconnect_ms && now_ms >= p.kick_disconnect_ms) {
			for (int i = 0; i < kDisconnectSends; ++i)
				send_wire_packet(p.address, p.port, p.id, NET_FLAG_DISCONNECT, 0, nullptr, 0, false);
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

	uint8_t buffer[4096];
	sockaddr_in from_addr{};

	while (true) {
		socklen_t from_len = sizeof(from_addr);
		ssize_t bytes = recvfrom(static_cast<SOCKET>(socket_fd_), reinterpret_cast<char*>(buffer), sizeof(buffer), 0, reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
		if (bytes <= 0) {
			break;
		}
		char ip_str[INET_ADDRSTRLEN] = {0};
		inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
		uint16_t from_port = ntohs(from_addr.sin_port);
		handle_incoming_packet(buffer, static_cast<size_t>(bytes), ip_str, from_port);
	}

	uint64_t now = get_now_ms();
	check_retransmissions(now);
	send_heartbeats(now);
	check_peer_timeouts(now);
	send_client_performance(now);
}

void NetHost::lose_host(int reason) {
	if (disconnect_reason_ >= 0 || peers_.empty()) return;
	disconnect_reason_ = reason;
	peers_[0].connected = false;
	std::cout << "[NetHost] Lost the connection to the host (reason " << reason << ")\n";
	if (peer_disconnected_cb_) peer_disconnected_cb_(0);
}

void NetHost::refusePeer(uint32_t peer_id) {
	NetPeer *p = isHost() && peer_id != 0 ? getPeer(peer_id) : nullptr;
	if (p && p->connected && !p->kick_disconnect_ms) p->kick_disconnect_ms = get_now_ms() + 2000;
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
		if (!p->player_name.empty()) banned_names_.insert(p->player_name);
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
	server_ip_.clear();
	server_port_ = 0;
	local_peer_id_ = 0;
	game_start_sent_ = false;
}

} // namespace rtso

