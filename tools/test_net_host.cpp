// NetHost session limits (0 A.D. NetServer.cpp MAX_CLIENTS, NDR_SERVER_FULL).
//
//   g++ -std=c++20 -O2 -Isrc tools/test_net_host.cpp src/network/net_host.cpp -o /tmp/test_net_host && /tmp/test_net_host
//
// 1. Clients that left do not count: after more than NET_MAX_CLIENTS
//    connections have come and gone, a new client still gets in.
// 2. With NET_MAX_CLIENTS connected, one more is told the server is full
//    (disconnect reason NDR_SERVER_FULL) instead of timing out.
// 3. 0 A.D. NetServer's handshake checks: a client on another engine version
//    or mods is refused (NDR_INCORRECT_SOFTWARE_VERSION); a name already in
//    the game, the host's included, is refused (NDR_PLAYERNAME_IN_USE) unless
//    network.duplicateplayernames, which lets it in as "Name (2)".
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

	// 3. Handshake checks, on a host of their own.
	NetHost game;
	game.setLocalPlayerName("alice");
	game.startHost(c);
	const uint16_t game_port = game.localPort();
	std::vector<std::string> admitted;
	game.onPeerConnected([&](uint32_t id) { admitted.push_back(game.getPeer(id)->player_name); });
	for (auto &p : live) p->stop();
	live.clear();
	// Each joiner stays connected (and polled) for the next ones.
	auto join = [&](const std::string &name, const std::string &version) {
		live.push_back(std::make_unique<NetHost>());
		NetHost &cl = *live.back();
		cl.setLocalPlayerName(name);
		if (!version.empty()) cl.setSoftwareVersion(version);
		cl.startClient("127.0.0.1", game_port);
		pump(game, live, 12000, [&] { return cl.connectionLost() || cl.authResult() >= 0; });
		return cl.authResult() == ARC_OK ? -1 : cl.disconnectReason();
	};
	CHECK(join("bob", "") == -1 && admitted == std::vector<std::string>{"bob"}, "bob joins");
	CHECK(join("carol", "RTS0-other") == NDR_INCORRECT_SOFTWARE_VERSION && admitted.size() == 1,
	      "another engine version is refused (NDR_INCORRECT_SOFTWARE_VERSION)");
	CHECK(join("bob", "") == NDR_PLAYERNAME_IN_USE && admitted.size() == 1,
	      "a second bob is refused (NDR_PLAYERNAME_IN_USE)");
	CHECK(join("alice", "") == NDR_PLAYERNAME_IN_USE && admitted.size() == 1, "so is the host's own name");
	game.setDuplicatePlayerNames(true);
	CHECK(join("bob", "") == -1 && join("bob", "") == -1 &&
	      admitted == (std::vector<std::string>{"bob", "bob (2)", "bob (3)"}),
	      "with network.duplicateplayernames they come in as bob (2) and bob (3)");
	for (auto &p : live) p->stop();

	if (failures) {
		std::printf("%d host check(s) failed\n", failures);
		return 1;
	}
	std::printf("all host checks passed\n");
	return 0;
}
