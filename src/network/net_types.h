#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <utility>

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
	NMT_JOIN_SYNC_START           = 16,
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
	NMT_COUNT                     = 31
};

enum AuthenticateResultCode : uint32_t {
	ARC_OK = 0,
	ARC_OK_SAVED_GAME = 1,
	ARC_OK_REJOINING = 2,
	ARC_PASSWORD_INVALID = 3
};

struct NetCommand {
	uint32_t client_id = 0;
	uint8_t cmd_type = 0; // 1=Move, 2=Stop, 3=Attack, 4=Gather, 5=Build, 6=Train, 7=Stance, 8=Formation
	std::vector<uint32_t> entity_ids;
	float target_x = 0.0f;
	float target_z = 0.0f;
	uint32_t target_id = 0;
	uint8_t param = 0;
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
	bool is_ready = false;
	bool is_authenticated = false;
	uint64_t last_seen_ms = 0;
	uint32_t last_sent_seq = 0;
	uint32_t last_recv_seq = 0;
	uint32_t last_ack_seq = 0;
};

struct NetConfig {
	uint16_t port = PS_DEFAULT_PORT; // 20595 (0x5073)
	uint8_t maxPlayers = 8;
	uint8_t maxObservers = 2;
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
	std::string engine_version = "RTS0-0.1";
	std::vector<std::pair<std::string, std::string>> enabled_mods;
};

struct ServerHandshakeMsg {
	uint32_t magic = PS_PROTOCOL_MAGIC;
	uint32_t protocol_version = PS_PROTOCOL_VERSION;
	std::string engine_version = "RTS0-0.1";
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
};

struct AuthenticateResultMsg {
	uint32_t code = ARC_OK;
	uint16_t host_id = 0;
	uint8_t is_controller = 0;
	std::string message;
};

struct ReadyMsg {
	std::string guid;
	uint8_t status = 1; // 1 = ready, 0 = not ready
};

struct GameStartMsg {
	std::string init_attributes;
};

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
	std::string sender_guid;
	std::string message;
	std::vector<std::string> receivers; // empty = broadcast to all
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
		return buf;
	}

	static bool parse_authenticate(const uint8_t *data, size_t size, AuthenticateMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.name)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.password)) return false;
		if (!NetSerde::read_string(data, size, offset, msg.controller_secret)) return false;
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
		return buf;
	}

	static bool parse_ready(const uint8_t *data, size_t size, ReadyMsg &msg) {
		size_t offset = 0;
		if (!NetSerde::read_string(data, size, offset, msg.guid)) return false;
		if (!NetSerde::read_u8(data, size, offset, msg.status)) return false;
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

