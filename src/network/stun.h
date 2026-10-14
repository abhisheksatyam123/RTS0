#pragma once
// STUN Binding messages (RFC 5389), as 0 A.D.'s source/network/StunClient.cpp
// sends them: a peer asks a STUN server, from its game socket, which public
// address and port its NAT maps that socket to, and the lobby hands that
// endpoint to the other side so both can punch a hole (0 A.D.
// SendHolePunchingMessages). Header-only so every tool that links net_host.cpp
// gets it without another source file.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rtso::stun {

constexpr uint32_t MAGIC_COOKIE = 0x2112A442u;
constexpr uint16_t BINDING_REQUEST = 0x0001;
constexpr uint16_t BINDING_SUCCESS = 0x0101;
constexpr uint16_t ATTR_MAPPED_ADDRESS = 0x0001;
constexpr uint16_t ATTR_XOR_MAPPED_ADDRESS = 0x0020;
constexpr size_t HEADER_SIZE = 20;
constexpr uint16_t DEFAULT_PORT = 3478; // 0 A.D. default.cfg lobby.stun.port

// The datagram a peer sends to open its NAT towards the other side (0 A.D.
// sends a few small ENet-less packets the other side ignores).
constexpr uint8_t HOLE_PUNCH[8] = {'R', 'T', 'S', '0', 'P', 'N', 'C', 'H'};

inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline void wr16(std::vector<uint8_t> &v, uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
inline void wr32(std::vector<uint8_t> &v, uint32_t x) {
	for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

inline std::vector<uint8_t> binding_request(const uint8_t txn[12]) {
	std::vector<uint8_t> v;
	wr16(v, BINDING_REQUEST);
	wr16(v, 0);
	wr32(v, MAGIC_COOKIE);
	v.insert(v.end(), txn, txn + 12);
	return v;
}

// A STUN message: the top two bits are zero and the cookie matches
// (RFC 5389 section 6), which no RTS0 wire packet or hole punch has.
inline bool is_stun(const uint8_t *d, size_t n) {
	return n >= HEADER_SIZE && (d[0] & 0xC0) == 0 && rd32(d + 4) == MAGIC_COOKIE &&
			HEADER_SIZE + rd16(d + 2) <= n;
}

inline bool is_hole_punch(const uint8_t *d, size_t n) {
	return n == sizeof(HOLE_PUNCH) && std::memcmp(d, HOLE_PUNCH, sizeof(HOLE_PUNCH)) == 0;
}

// The mapped IPv4 address and port of a Binding Success answering txn.
// XOR-MAPPED-ADDRESS wins over the older MAPPED-ADDRESS (0 A.D. reads both).
inline bool parse_binding_success(const uint8_t *d, size_t n, const uint8_t txn[12], std::string &ip, uint16_t &port) {
	if (!is_stun(d, n) || rd16(d) != BINDING_SUCCESS || std::memcmp(d + 8, txn, 12) != 0) return false;
	const size_t end = HEADER_SIZE + rd16(d + 2);
	bool found = false;
	uint32_t addr = 0;
	for (size_t off = HEADER_SIZE; off + 4 <= end;) {
		const uint16_t type = rd16(d + off), len = rd16(d + off + 2);
		const uint8_t *val = d + off + 4;
		if (off + 4 + len > end) break;
		// value: reserved, family (1 = IPv4), port, address
		if ((type == ATTR_XOR_MAPPED_ADDRESS || (type == ATTR_MAPPED_ADDRESS && !found)) && len >= 8 && val[1] == 0x01) {
			port = rd16(val + 2);
			addr = rd32(val + 4);
			if (type == ATTR_XOR_MAPPED_ADDRESS) {
				port ^= (uint16_t)(MAGIC_COOKIE >> 16);
				addr ^= MAGIC_COOKIE;
			}
			found = true;
			if (type == ATTR_XOR_MAPPED_ADDRESS) break;
		}
		off += 4 + ((len + 3u) & ~3u);
	}
	if (!found) return false;
	ip = std::to_string(addr >> 24) + "." + std::to_string((addr >> 16) & 255) + "." +
			std::to_string((addr >> 8) & 255) + "." + std::to_string(addr & 255);
	return true;
}

// A server's answer (tools/lobby_server.py answers the same way; tests use this).
inline std::vector<uint8_t> binding_success(const uint8_t txn[12], uint32_t ipv4, uint16_t port) {
	std::vector<uint8_t> v;
	wr16(v, BINDING_SUCCESS);
	wr16(v, 12);
	wr32(v, MAGIC_COOKIE);
	v.insert(v.end(), txn, txn + 12);
	wr16(v, ATTR_XOR_MAPPED_ADDRESS);
	wr16(v, 8);
	v.push_back(0);
	v.push_back(0x01);
	wr16(v, (uint16_t)(port ^ (MAGIC_COOKIE >> 16)));
	wr32(v, ipv4 ^ MAGIC_COOKIE);
	return v;
}

} // namespace rtso::stun
