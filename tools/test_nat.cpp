// NAT traversal tests (0 A.D. StunClient.cpp + Jingle hole punching).
//
//   g++ -std=c++20 -O2 -Isrc tools/test_nat.cpp src/network/net_host.cpp -o /tmp/test_nat && /tmp/test_nat
//
// 1. STUN Binding messages encode and parse (RFC 5389), and neither RTS0 wire
//    packets nor hole punches read as STUN.
// 2. A NetHost learns its public endpoint from a STUN server on its own game
//    socket, and gives up when no server answers.
// 3. With both peers behind a simulated stateful firewall (restricted-cone
//    NAT), a client cannot reach the host until the host punches a hole to
//    the client's STUN endpoint; then it connects.
// 4. retargetHost moves a connecting client to another address (0 A.D. uses
//    the host's LAN address when both share a public one).
#include "network/net_host.h"
#include "network/stun.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

using namespace rtso;

static int failures = 0;
#define CHECK(cond, what)                                                        \
	do {                                                                         \
		if (cond) std::printf("PASS %s\n", what);                                \
		else { std::printf("FAIL %s (%s:%d)\n", what, __FILE__, __LINE__); ++failures; } \
	} while (0)

static uint64_t now_ms() {
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// A STUN server on 127.0.0.1 that answers every Binding Request with the
// address it came from (tools/lobby_server.py's responder does the same).
struct StunServer {
	int fd = -1;
	uint16_t port = 0;
	int answered = 0;
	bool silent = false;
	StunServer() {
		fd = socket(AF_INET, SOCK_DGRAM, 0);
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bind(fd, (sockaddr *)&a, sizeof(a));
		socklen_t len = sizeof(a);
		getsockname(fd, (sockaddr *)&a, &len);
		port = ntohs(a.sin_port);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	}
	~StunServer() { close(fd); }
	void poll() {
		uint8_t buf[512];
		sockaddr_in from{};
		socklen_t len = sizeof(from);
		ssize_t n;
		while ((n = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&from, &len)) > 0) {
			len = sizeof(from);
			if (silent || !stun::is_stun(buf, (size_t)n) || stun::rd16(buf) != stun::BINDING_REQUEST) continue;
			auto resp = stun::binding_success(buf + 8, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port));
			sendto(fd, resp.data(), resp.size(), 0, (sockaddr *)&from, sizeof(from));
			++answered;
		}
	}
};

template <class Pred>
static bool pump(std::initializer_list<NetHost *> hosts, StunServer *stun, uint64_t ms, Pred done) {
	Arena arena(1 << 16);
	const uint64_t until = now_ms() + ms;
	while (now_ms() < until) {
		for (NetHost *h : hosts) h->poll(arena);
		if (stun) stun->poll();
		arena.reset();
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return done();
}

static void test_messages() {
	uint8_t txn[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
	auto req = stun::binding_request(txn);
	CHECK(req.size() == 20 && stun::is_stun(req.data(), req.size()), "a Binding Request is a 20-byte STUN message");
	const uint32_t ip = (203u << 24) | (0u << 16) | (113u << 8) | 7u;
	auto ok = stun::binding_success(txn, ip, 40123);
	std::string got;
	uint16_t port = 0;
	CHECK(stun::parse_binding_success(ok.data(), ok.size(), txn, got, port) && got == "203.0.113.7" && port == 40123,
	      "XOR-MAPPED-ADDRESS decodes to 203.0.113.7:40123");
	uint8_t other[12] = {9};
	CHECK(!stun::parse_binding_success(ok.data(), ok.size(), other, got, port), "an answer to another transaction is ignored");
	// The older MAPPED-ADDRESS, as a pre-5389 server sends it.
	std::vector<uint8_t> old = {0x01, 0x01, 0, 12};
	stun::wr32(old, stun::MAGIC_COOKIE);
	old.insert(old.end(), txn, txn + 12);
	stun::wr16(old, stun::ATTR_MAPPED_ADDRESS);
	stun::wr16(old, 8);
	old.push_back(0);
	old.push_back(1);
	stun::wr16(old, 5000);
	stun::wr32(old, (192u << 24) | (168u << 16) | (1u << 8) | 20u);
	CHECK(stun::parse_binding_success(old.data(), old.size(), txn, got, port) && got == "192.168.1.20" && port == 5000,
	      "MAPPED-ADDRESS decodes too");
	NetWireHeader hdr;
	CHECK(!stun::is_stun((const uint8_t *)&hdr, sizeof(hdr)), "an RTS0 wire packet is not STUN");
	CHECK(!stun::is_stun(stun::HOLE_PUNCH, sizeof(stun::HOLE_PUNCH)) &&
	      stun::is_hole_punch(stun::HOLE_PUNCH, sizeof(stun::HOLE_PUNCH)), "a hole punch is not STUN");
}

static void test_query() {
	StunServer srv;
	NetHost host;
	NetConfig c;
	c.port = 0;
	host.startHost(c);
	const uint16_t local = host.localPort();
	CHECK(local != 0, "the host's game socket has a port");
	host.queryStun("127.0.0.1", srv.port);
	CHECK(host.stunState() == 1, "a STUN query is pending");
	pump({&host}, &srv, 2000, [&] { return host.stunState() != 1; });
	CHECK(host.stunState() == 2 && host.publicIp() == "127.0.0.1" && host.publicPort() == local,
	      "the STUN server names the game socket's own endpoint");
	host.stop();

	StunServer quiet;
	quiet.silent = true;
	NetHost lone;
	lone.startHost(c);
	const uint64_t t0 = now_ms();
	lone.queryStun("127.0.0.1", quiet.port);
	pump({&lone}, &quiet, NetHost::STUN_TIMEOUT_MS + 1000, [&] { return lone.stunState() != 1; });
	CHECK(lone.stunState() == 3 && now_ms() - t0 >= NetHost::STUN_TIMEOUT_MS,
	      "no answer: the query fails after STUN_TIMEOUT_MS");
	NetHost bad;
	bad.startHost(c);
	bad.queryStun("no-such-host.invalid", 3478);
	CHECK(bad.stunState() == 3, "an unresolvable STUN server fails at once");
	const std::string lan = NetHost::localIp();
	in_addr tmp{};
	CHECK(lan.empty() || inet_pton(AF_INET, lan.c_str(), &tmp) == 1, "localIp is a dotted quad or empty");
}

static void test_hole_punch() {
	StunServer srv;
	NetHost host, client;
	NetConfig c;
	c.port = 0;
	host.setLocalPlayerName("alice");
	host.startHost(c);
	host.set_simulated_firewall(true);
	host.queryStun("127.0.0.1", srv.port);
	pump({&host}, &srv, 2000, [&] { return host.stunState() != 1; });
	CHECK(host.stunState() == 2, "a firewalled host still hears its STUN answer");

	int joined = 0;
	host.onPeerConnected([&](uint32_t) { ++joined; });
	client.setLocalPlayerName("bob");
	client.startClient(host.publicIp(), host.publicPort());
	client.set_simulated_firewall(true);
	client.queryStun("127.0.0.1", srv.port);
	pump({&host, &client}, &srv, 3000, [&] { return false; });
	CHECK(client.stunState() == 2, "the client learns its endpoint from the same socket");
	CHECK(joined == 0 && client.authResult() < 0 && host.firewalledPackets() > 0,
	      "without a hole punch the host's firewall drops the client");

	// 0 A.D.: the client sends its STUN endpoint through the lobby, the host
	// punches towards it, and the client's next connect resend gets in.
	host.holePunch(client.publicIp(), client.publicPort());
	client.holePunch(host.publicIp(), host.publicPort());
	const bool in = pump({&host, &client}, &srv, 5000, [&] { return joined == 1 && client.authResult() == ARC_OK; });
	CHECK(in, "after the hole punch the client connects and authenticates");
}

static void test_retarget() {
	NetHost host, client;
	NetConfig c;
	c.port = 0;
	host.startHost(c);
	int joined = 0;
	host.onPeerConnected([&](uint32_t) { ++joined; });
	// A port nothing listens on, standing for a public address this
	// network's router does not loop back to.
	NetHost dead;
	dead.startHost(c);
	const uint16_t nowhere = dead.localPort();
	dead.stop();
	client.startClient("127.0.0.1", nowhere);
	pump({&host, &client}, nullptr, 600, [&] { return false; });
	CHECK(joined == 0, "a client aimed at a dead address does not connect");
	client.retargetHost("127.0.0.1", host.localPort());
	const bool in = pump({&host, &client}, nullptr, 4000, [&] { return joined == 1 && client.authResult() == ARC_OK; });
	CHECK(in, "retargetHost moves the queued connect to the host's other address");
}

int main() {
	test_messages();
	test_query();
	test_hole_punch();
	test_retarget();
	if (failures) {
		std::printf("%d NAT check(s) failed\n", failures);
		return 1;
	}
	std::printf("all NAT checks passed\n");
	return 0;
}
