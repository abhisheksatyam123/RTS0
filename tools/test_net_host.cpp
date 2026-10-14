// NetHost session limits (0 A.D. NetServer.cpp MAX_CLIENTS, NDR_SERVER_FULL).
//
//   g++ -std=c++20 -O2 -Isrc tools/test_net_host.cpp src/network/net_host.cpp -o /tmp/test_net_host && /tmp/test_net_host
//
// 1. Clients that left do not count: after more than NET_MAX_CLIENTS
//    connections have come and gone, a new client still gets in.
// 2. With NET_MAX_CLIENTS connected, one more is told the server is full
//    (disconnect reason NDR_SERVER_FULL) instead of timing out.
#include "network/net_host.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

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

template <class Pred>
static bool pump(NetHost &host, std::vector<std::unique_ptr<NetHost>> &clients, uint64_t ms, Pred done) {
	Arena arena(1 << 16);
	const uint64_t until = now_ms() + ms;
	while (now_ms() < until) {
		host.poll(arena);
		for (auto &c : clients) c->poll(arena);
		arena.reset();
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return done();
}

int main() {
	NetHost host;
	NetConfig c;
	c.port = 0;
	host.startHost(c);
	const uint16_t port = host.localPort();
	int joined = 0;
	host.onPeerConnected([&](uint32_t) { ++joined; });

	// 1. Come and go, more times than the cap.
	std::vector<std::unique_ptr<NetHost>> none;
	const int rounds = (int)NET_MAX_CLIENTS + 4;
	for (int i = 0; i < rounds; ++i) {
		std::vector<std::unique_ptr<NetHost>> one;
		one.push_back(std::make_unique<NetHost>());
		one[0]->setLocalPlayerName("guest" + std::to_string(i));
		one[0]->startClient("127.0.0.1", port);
		const int before = joined;
		pump(host, one, 3000, [&] { return joined > before && one[0]->authResult() == ARC_OK; });
		one[0]->stop();
		pump(host, none, 200, [&] { return false; });
	}
	CHECK(joined == rounds, "every client gets in although more than NET_MAX_CLIENTS came and went");

	// 2. Fill the session, then one more.
	std::vector<std::unique_ptr<NetHost>> live;
	for (size_t i = 0; i < NET_MAX_CLIENTS; ++i) {
		live.push_back(std::make_unique<NetHost>());
		live.back()->setLocalPlayerName("p" + std::to_string(i));
		live.back()->startClient("127.0.0.1", port);
	}
	const int base = joined;
	pump(host, live, 8000, [&] { return joined - base == (int)NET_MAX_CLIENTS; });
	CHECK(joined - base == (int)NET_MAX_CLIENTS, "NET_MAX_CLIENTS clients connect at once");
	live.push_back(std::make_unique<NetHost>());
	NetHost &extra = *live.back();
	extra.setLocalPlayerName("late");
	extra.startClient("127.0.0.1", port);
	pump(host, live, 3000, [&] { return extra.connectionLost(); });
	CHECK(extra.connectionLost() && extra.disconnectReason() == NDR_SERVER_FULL && joined - base == (int)NET_MAX_CLIENTS,
	      "one more is told the server is full (NDR_SERVER_FULL)");

	if (failures) {
		std::printf("%d host check(s) failed\n", failures);
		return 1;
	}
	std::printf("all host checks passed\n");
	return 0;
}
