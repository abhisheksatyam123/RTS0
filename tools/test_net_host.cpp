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
// 4. 0 A.D. NetServer CheckPasswordAndIncrement / IsBanned: three wrong
//    passwords from one name ban it, so even the right one is then refused
//    (ARC_BANNED); the right password before the third resets the count.
// 5. 0 A.D. NetServer OnChat with ChatAddressees.isAddressee
//    (network/chat_routing.h): the host relays an ally, enemy, observer,
//    private or flare line only to its addressees, and an untagged one to all.
// 6. Before Start a joiner reuses the lowest free id 1-7 (the setup seats and
//    lists only clients 0-7), however many came and went; after Start ids stay
//    unique, since the turn manager still knows a dropped client's id.
// 7. 0.28 reportConnectionRequestTimeOut: a connect nobody answers gives up
//    (reason 0) instead of waiting forever.
#include "network/net_host.h"
#include "network/chat_routing.h"

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <map>
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
	uint32_t max_id = 0;
	host.onPeerConnected([&](uint32_t id) { ++joined; max_id = std::max(max_id, id); });

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
	CHECK(max_id == 1, "before Start each of them reuses peer id 1, so the setup can seat it");

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
	live.clear();

	// 4. Wrong passwords, on a host with one.
	NetHost locked;
	locked.setLocalPlayerName("alice");
	locked.setPassword("hunter2");
	locked.startHost(c);
	const uint16_t locked_port = locked.localPort();
	auto attempt = [&](const std::string &name, const std::string &password) {
		std::vector<std::unique_ptr<NetHost>> one;
		one.push_back(std::make_unique<NetHost>());
		NetHost &cl = *one[0];
		cl.setLocalPlayerName(name);
		cl.setPassword(password);
		cl.startClient("127.0.0.1", locked_port);
		pump(locked, one, 5000, [&] { return cl.authResult() >= 0 || cl.connectionLost(); });
		const int code = cl.authResult();
		cl.stop();
		pump(locked, none, 100, [&] { return false; });
		return code;
	};
	CHECK(attempt("dan", "x") == ARC_PASSWORD_INVALID && attempt("dan", "y") == ARC_PASSWORD_INVALID &&
	      attempt("dan", "hunter2") == ARC_OK && attempt("dan", "z") == ARC_PASSWORD_INVALID &&
	      attempt("dan", "hunter2") == ARC_OK,
	      "two wrong passwords, then the right one, let dan in (and reset his count)");
	CHECK(attempt("eve", "a") == ARC_PASSWORD_INVALID && attempt("eve", "b") == ARC_PASSWORD_INVALID &&
	      attempt("eve", "c") == ARC_PASSWORD_INVALID,
	      "eve's three wrong passwords are refused as wrong");
	CHECK(attempt("eve", "hunter2") == ARC_BANNED, "then eve is banned, even with the right password");
	CHECK(attempt("frank", "hunter2") == ARC_OK, "another name still gets in");

	// 5. Chat routing. Seats: host 0 and client A 1 are allies, client B 2 is
	// their enemy, client O watches (255).
	NetHost room;
	room.setLocalPlayerName("host");
	room.startHost(c);
	const uint16_t room_port = room.localPort();
	std::vector<std::unique_ptr<NetHost>> people;
	std::map<std::string, std::vector<std::string>> heard; // who -> lines
	std::vector<std::string> host_heard;
	room.onChat([&](const ChatMsg &m) { host_heard.push_back(m.message); });
	for (const char *name : {"A", "B", "O"}) {
		people.push_back(std::make_unique<NetHost>());
		NetHost &cl = *people.back();
		cl.setLocalPlayerName(name);
		const std::string who = name;
		cl.onChat([&heard, who](const ChatMsg &m) { heard[who].push_back(m.message); });
		cl.startClient("127.0.0.1", room_port);
	}
	pump(room, people, 8000, [&] {
		for (auto &p : people) if (p->authResult() != ARC_OK) return false;
		return true;
	});
	std::map<uint32_t, uint8_t> seat{{0, 0}};
	seat[room.peerByName("A")] = 1;
	seat[room.peerByName("B")] = 2;
	seat[room.peerByName("O")] = 255;
	auto allied = [](uint8_t a, uint8_t b) { return (a <= 1 && b <= 1) || a == b; };
	room.setChatRouter([&](uint32_t from, uint32_t to, const std::string &msg) {
		const std::string tag = chat_wire_tag(msg);
		return chat_tag_reaches(seat[from], seat[to], tag, 8, allied,
				[&](uint8_t a, uint8_t b) { return !allied(a, b); });
	});
	auto say = [&](NetHost &from, const std::string &line) {
		from.sendChat(line);
		pump(room, people, 600, [&] { return false; });
	};
	NetHost &a = *people[0], &b = *people[1], &o = *people[2];
	say(a, "[Allies] push left");
	say(a, "[Enemies] gg");
	say(b, "[Player-2] psst");
	say(o, "[Observers] nice");
	say(o, "[Player-2] hi player");
	say(room, "[Flare] 10,20");
	say(b, "hello all");
	auto has = [&](const std::string &who, const std::string &line) {
		const auto &v = who == "host" ? host_heard : heard[who];
		return std::find(v.begin(), v.end(), line) != v.end();
	};
	CHECK(has("host", "[Allies] push left") && !has("B", "[Allies] push left") && !has("O", "[Allies] push left"),
	      "an ally line reaches only the sender's allies");
	CHECK(has("B", "[Enemies] gg") && !has("host", "[Enemies] gg") && !has("O", "[Enemies] gg"),
	      "an enemy line reaches only the sender's enemies");
	CHECK(has("A", "[Player-2] psst") && !has("host", "[Player-2] psst") && !has("O", "[Player-2] psst"),
	      "a private line reaches only its player");
	CHECK(!has("A", "[Player-2] hi player") && !has("host", "[Player-2] hi player"),
	      "an observer cannot message a player privately");
	CHECK(!has("host", "[Observers] nice") && !has("A", "[Observers] nice") && !has("B", "[Observers] nice"),
	      "an observer line reaches no player");
	CHECK(has("A", "[Flare] 10,20") && has("O", "[Flare] 10,20") && !has("B", "[Flare] 10,20"),
	      "a flare reaches allies and observers, not enemies");
	CHECK(has("host", "hello all") && has("A", "hello all") && has("O", "hello all"), "an untagged line reaches everyone");
	for (auto &p : people) p->stop();

	// 6. After Start, a dropped id is not handed out again.
	{
		NetHost started;
		started.startHost(c);
		const uint16_t started_port = started.localPort();
		std::vector<uint32_t> ids;
		started.onPeerConnected([&](uint32_t id) { ids.push_back(id); });
		started.note_game_start_sent();
		for (int i = 0; i < 3; ++i) {
			std::vector<std::unique_ptr<NetHost>> one;
			one.push_back(std::make_unique<NetHost>());
			one[0]->setLocalPlayerName("late" + std::to_string(i));
			one[0]->startClient("127.0.0.1", started_port);
			pump(started, one, 3000, [&] { return ids.size() > (size_t)i; });
			one[0]->stop();
			pump(started, none, 200, [&] { return false; });
		}
		CHECK(ids == (std::vector<uint32_t>{1, 2, 3}), "after Start each joiner gets a new peer id");
	}

	// 7. Nobody listens on the port.
	{
		uint16_t dead_port = 0;
		{
			NetHost gone;
			gone.startHost(c);
			dead_port = gone.localPort();
			gone.stop();
		}
		std::vector<std::unique_ptr<NetHost>> lonely;
		lonely.push_back(std::make_unique<NetHost>());
		lonely[0]->setLocalPlayerName("lonely");
		lonely[0]->startClient("127.0.0.1", dead_port);
		NetHost idle;
		idle.startHost(c);
		const uint64_t t0 = now_ms();
		pump(idle, lonely, 20000, [&] { return lonely[0]->connectionLost(); });
		const uint64_t took = now_ms() - t0;
		CHECK(lonely[0]->connectionLost() && lonely[0]->disconnectReason() == 0 && took >= 10000 && took < 15000,
		      "a connect nobody answers times out after about 11 s (reason 0)");
	}

	if (failures) {
		std::printf("%d host check(s) failed\n", failures);
		return 1;
	}
	std::printf("all host checks passed\n");
	return 0;
}
