// NetHost session limits (0 A.D. NetServer.cpp MAX_CLIENTS, NDR_SERVER_FULL).
//
//   g++ -std=c++20 -O2 -Isrc tools/test_net_host.cpp src/network/net_host.cpp -o /tmp/test_net_host && /tmp/test_net_host
//
// 1. Clients that left do not count: after more than NET_MAX_CLIENTS
//    connections have come and gone, a new client still gets in.
//    A host name is resolved once at the join (CNetClientSession::Connect),
//    and one that does not resolve fails it at once.
// 2. With NET_MAX_REMOTE_CLIENTS connected, one more is told the server is full
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
// 6. Before Start a joiner reuses the lowest free id (the setup seats and
//    lists clients 0 to NET_SETUP_CLIENTS-1), however many came and went; after Start ids stay
//    unique, since the turn manager still knows a dropped client's id.
// 7. 0.28 reportConnectionRequestTimeOut: a connect nobody answers gives up
//    (reason 0) instead of waiting forever.
// 8. 0.28 polls the network on its own thread, so a peer whose main thread is
//    busy (loading the map at Start) is not timed out. NetHost::KeepAlive
//    sends its heartbeats meanwhile: a host stuck for 12 s keeps its client,
//    and a client stuck for 12 s keeps its seat; without it the client is lost.
// 9. ENet's reliable channel: a message larger than a datagram (a stalled
//    turn's batch, a rejoin header) arrives whole, split under the 1372-byte
//    MTU, and reliable messages arrive in the order sent, also with 30% loss.
// 10. 0.28 NetServer's late-joiner refusals: a refused client is told why
//     (NDR_SERVER_ALREADY_IN_GAME), not "The host has ended the game." (2).
// 12. 0 A.D. SanitisePlayerName: no GUI tags, at most 32 characters, trimmed,
//     "Anonymous" for blanks.
// 11. 0.28 lobby authentication (NetServer m_LobbyAuth): a lobby host asks
//     the lobby whose ticket a joiner sent and admits it only under that
//     name (case and a " (rating)" aside); another name, an unknown ticket or
//     a direct join without one is refused with NDR_LOBBY_AUTH_FAILED.
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
	{
		// CNetClientSession::Connect: a host name is resolved once, and one that
		// does not resolve fails the join at once.
		std::vector<std::unique_ptr<NetHost>> named;
		named.push_back(std::make_unique<NetHost>());
		CHECK(!named[0]->startClient("no-such-host.invalid", port), "a host name that does not resolve fails at once");
		named[0]->setLocalPlayerName("byname");
		CHECK(named[0]->startClient("localhost", port), "a host name that resolves starts the join");
		const int before = joined;
		pump(host, named, 3000, [&] { return joined > before && named[0]->authResult() == ARC_OK; });
		CHECK(joined > before, "and connects to the resolved address");
		named[0]->stop();
		pump(host, none, 200, [&] { return false; });
	}
	CHECK(max_id == 1, "before Start each of them reuses peer id 1, so the setup can seat it");

	// 2. Fill the session, then one more.
	std::vector<std::unique_ptr<NetHost>> live;
	for (size_t i = 0; i < NET_MAX_REMOTE_CLIENTS; ++i) {
		live.push_back(std::make_unique<NetHost>());
		live.back()->setLocalPlayerName("p" + std::to_string(i));
		live.back()->startClient("127.0.0.1", port);
	}
	const int base = joined;
	max_id = 0;
	pump(host, live, 8000, [&] { return joined - base == (int)NET_MAX_REMOTE_CLIENTS; });
	CHECK(joined - base == (int)NET_MAX_REMOTE_CLIENTS, "NET_MAX_REMOTE_CLIENTS clients connect at once");
	CHECK(max_id == NET_MAX_REMOTE_CLIENTS && max_id < NET_SETUP_CLIENTS, "with ids 1 to NET_MAX_REMOTE_CLIENTS, all inside the setup's masks");
	live.push_back(std::make_unique<NetHost>());
	NetHost &extra = *live.back();
	extra.setLocalPlayerName("late");
	extra.startClient("127.0.0.1", port);
	pump(host, live, 3000, [&] { return extra.connectionLost(); });
	CHECK(extra.connectionLost() && extra.disconnectReason() == NDR_SERVER_FULL && joined - base == (int)NET_MAX_REMOTE_CLIENTS,
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
	auto join = [&](const std::string &name, const std::string &version,
			std::vector<std::pair<std::string, std::string>> mods = {}) {
		live.push_back(std::make_unique<NetHost>());
		NetHost &cl = *live.back();
		cl.setLocalPlayerName(name);
		if (!version.empty()) cl.setSoftwareVersion(version);
		cl.setEnabledMods(std::move(mods));
		cl.startClient("127.0.0.1", game_port);
		pump(game, live, 12000, [&] { return cl.connectionLost() || cl.authResult() >= 0; });
		return cl.authResult() == ARC_OK ? -1 : cl.disconnectReason();
	};
	CHECK(join("bob", "") == -1 && admitted == std::vector<std::string>{"bob"}, "bob joins");
	CHECK(join("carol", "RTS0-other") == NDR_INCORRECT_SOFTWARE_VERSION && admitted.size() == 1,
	      "another engine version is refused (NDR_INCORRECT_SOFTWARE_VERSION)");
	CHECK(live.back()->hostSoftwareVersion() == game.softwareVersion() && live.back()->softwareVersion() == "RTS0-other",
	      "the refused client knows the host's version, for the mismatch detail");
	CHECK(join("dave", "", {{"public", "0.28.0"}, {"mymod", "1.0"}}) == NDR_INCORRECT_SOFTWARE_VERSION &&
	      admitted.size() == 1, "other enabled mods are refused (NDR_INCORRECT_SOFTWARE_VERSION)");
	CHECK(join("bob", "") == NDR_PLAYERNAME_IN_USE && admitted.size() == 1,
	      "a second bob is refused (NDR_PLAYERNAME_IN_USE)");
	CHECK(join("alice", "") == NDR_PLAYERNAME_IN_USE && admitted.size() == 1, "so is the host's own name");
	game.setDuplicatePlayerNames(true);
	CHECK(join("bob", "") == -1 && join("bob", "") == -1 &&
	      admitted == (std::vector<std::string>{"bob", "bob (2)", "bob (3)"}),
	      "with network.duplicateplayernames they come in as bob (2) and bob (3)");
	game.setServerLoading(true);
	CHECK(join("erin", "") == NDR_SERVER_LOADING && admitted.size() == 3,
	      "a join while the started game loads is refused (NDR_SERVER_LOADING)");
	game.setServerLoading(false);
	CHECK(join("erin", "") == -1 && admitted.size() == 4, "once it has loaded, erin joins");
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
	// With a hasher (0 A.D. HashCryptographically) the client sends hash(password,
	// name), never the text, and the host checks it against its own password.
	{
		auto toy = [](const std::string &pw, const std::string &name) { return "H(" + pw + "|" + name + ")"; };
		NetHost hashed;
		hashed.setPassword("hunter2");
		hashed.setPasswordHasher(toy);
		hashed.startHost(c);
		auto try_hashed = [&](const std::string &name, const std::string &password, bool hasher) {
			std::vector<std::unique_ptr<NetHost>> one;
			one.push_back(std::make_unique<NetHost>());
			NetHost &cl = *one[0];
			cl.setLocalPlayerName(name);
			cl.setPassword(password);
			if (hasher) cl.setPasswordHasher(toy);
			cl.startClient("127.0.0.1", hashed.localPort());
			pump(hashed, one, 5000, [&] { return cl.authResult() >= 0 || cl.connectionLost(); });
			const int code = cl.authResult();
			cl.stop();
			pump(hashed, none, 100, [&] { return false; });
			return code;
		};
		CHECK(try_hashed("gina", "hunter2", true) == ARC_OK, "a hashed password is accepted");
		CHECK(try_hashed("hal", "hunter2", false) == ARC_PASSWORD_INVALID, "the plain text is not");
		CHECK(try_hashed("ivy", "nope", true) == ARC_PASSWORD_INVALID, "a wrong hashed password is refused");
		hashed.stop();
	}

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
	for (const char *name : {"A", "B", "O", "P"}) {
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
	seat[room.peerByName("P")] = 255;
	auto allied = [](uint8_t a, uint8_t b) { return (a <= 1 && b <= 1) || a == b; };
	room.setChatRouter([&](uint32_t from, uint32_t to, const std::string &msg) {
		const std::string tag = chat_wire_tag(msg);
		return chat_tag_reaches(seat[from], seat[to], tag, 8, allied,
				[&](uint8_t a, uint8_t b) { return !allied(a, b); }, to);
	});
	auto say = [&](NetHost &from, const std::string &line) {
		from.sendChat(line);
		pump(room, people, 600, [&] { return false; });
	};
	NetHost &a = *people[0], &b = *people[1], &o = *people[2];
	const std::string to_p = "[Observer-" + std::to_string(room.peerByName("P")) + "]";
	say(a, to_p + " watch this");
	say(o, to_p + " from O");
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
	CHECK(has("P", to_p + " watch this") && !has("O", to_p + " watch this") && !has("B", to_p + " watch this") &&
	      !has("host", to_p + " watch this"), "a private line to one observer reaches only that observer");
	CHECK(has("P", to_p + " from O") && !has("A", to_p + " from O"), "an observer can message another observer privately");
	CHECK(chat_wire_tag("[Observer-12] x") == "[Observer-12]" && chat_wire_tag("[Observer-] x").empty() &&
	      chat_wire_tag("[Observer-99999999999] x").empty(), "the observer tag is parsed strictly");
	CHECK(chat_wire_tag("[All] [Player-3] hi").empty() && chat_wire_tag("[All] [Flare] 1,2").empty(),
	      "a typed tag behind [All] travels to everyone, as text");
	CHECK(has("host", "hello all") && has("A", "hello all") && has("O", "hello all"), "an untagged line reaches everyone");
	{
		// A player whose game is over is an observer for chat (0.28 isPlayerObserver):
		// player 1 defeated, player 0 active and allied with it, 9 a seatless observer.
		auto ally = [](uint8_t, uint8_t) { return true; };
		auto enemy = [](uint8_t, uint8_t) { return false; };
		auto reach = [&](uint8_t from, uint8_t to, const char *tag, bool from_done, bool to_done) {
			return chat_tag_reaches(from, to, tag, 8, ally, enemy, 7, from_done, to_done);
		};
		CHECK(reach(9, 1, "[Observers]", false, true) && !reach(9, 0, "[Observers]", false, false),
		      "observer chat reaches a defeated player, not an active one");
		CHECK(reach(1, 9, "[Player-0]", true, false) && reach(9, 1, "[Player-2]", false, true),
		      "a defeated player and an observer can message each other privately");
		CHECK(!reach(1, 0, "[Player-1]", true, false), "a defeated player cannot message an active one privately");
		CHECK(reach(0, 1, "[Player-2]", false, true), "an active player can message a defeated one");
		CHECK(reach(0, 1, "[Allies]", false, true) && reach(0, 1, "[Flare]", false, true) && reach(1, 0, "[Flare]", true, false),
		      "allies and flares still go by the seat");
		CHECK(!reach(0, 1, "[Observer-7]", false, true) && reach(0, 9, "[Observer-7]", false, false),
		      "an [Observer-C] line reaches only that seatless client");
	}
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

	// 8. A peer that does not poll for 12 s.
	{
		auto session = [&](NetHost &h, std::vector<std::unique_ptr<NetHost>> &cl) {
			h.startHost(c);
			cl.push_back(std::make_unique<NetHost>());
			cl[0]->setLocalPlayerName("loader");
			cl[0]->startClient("127.0.0.1", h.localPort());
			return pump(h, cl, 3000, [&] { return cl[0]->getLocalPeerId() != 0; });
		};
		auto client_kept = [](NetHost &h) {
			for (const auto &p : h.getPeers())
				if (p.id != 0 && p.connected) return true;
			return false;
		};
		Arena arena(1 << 16);
		auto poll_only = [&](NetHost &who, uint64_t ms) {
			const uint64_t until = now_ms() + ms;
			while (now_ms() < until) {
				who.poll(arena);
				arena.reset();
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		{
			NetHost h;
			std::vector<std::unique_ptr<NetHost>> cl;
			CHECK(session(h, cl), "8: the loader joins");
			{
				NetHost::KeepAlive keep(h);
				poll_only(*cl[0], 12000); // the host loads
			}
			pump(h, cl, 1500, [&] { return false; });
			CHECK(!cl[0]->connectionLost() && client_kept(h), "a host that loads for 12 s keeps its client");
		}
		{
			NetHost h;
			std::vector<std::unique_ptr<NetHost>> cl;
			session(h, cl);
			{
				NetHost::KeepAlive keep(*cl[0]);
				poll_only(h, 12000); // the client loads
			}
			pump(h, cl, 1500, [&] { return false; });
			CHECK(!cl[0]->connectionLost() && client_kept(h), "a client that loads for 12 s keeps its seat");
		}
		{
			NetHost h;
			std::vector<std::unique_ptr<NetHost>> cl;
			session(h, cl);
			poll_only(*cl[0], 12000);
			CHECK(cl[0]->connectionLost(), "without it the client loses a host silent for 12 s");
		}
	}

	// 9. Big and ordered messages.
	{
		NetHost h;
		h.startHost(c);
		std::vector<std::unique_ptr<NetHost>> cl;
		cl.push_back(std::make_unique<NetHost>());
		cl[0]->setLocalPlayerName("big");
		cl[0]->startClient("127.0.0.1", h.localPort());
		CHECK(pump(h, cl, 3000, [&] { return cl[0]->getLocalPeerId() != 0; }), "9: the client joins");
		std::vector<std::vector<uint8_t>> got;
		cl[0]->onMessage([&](uint32_t, const NetMessage &m) {
			if (m.type == static_cast<uint8_t>(NetMessageType::NMT_SIMULATION_COMMAND)) got.push_back(m.payload);
		});
		cl[0]->onCommand([&](uint32_t, const uint8_t *, size_t) {});
		h.set_simulated_loss(0.0f);
		std::vector<uint8_t> big(20000);
		for (size_t i = 0; i < big.size(); ++i) big[i] = (uint8_t)(i * 7 + 3);
		h.sendSimulationCommand(cl[0]->getLocalPeerId(), 5, big);
		pump(h, cl, 3000, [&] { return !got.empty(); });
		CHECK(got.size() == 1 && got[0] == big, "a 20 KB message arrives whole");
		got.clear();
		h.set_simulated_loss(0.3f, 7);
		cl[0]->set_simulated_loss(0.3f, 11);
		for (uint32_t i = 0; i < 60; ++i) {
			std::vector<uint8_t> m(i % 3 == 0 ? 3000 : 8, (uint8_t)i);
			h.sendSimulationCommand(cl[0]->getLocalPeerId(), i, m);
			pump(h, cl, 10, [&] { return false; });
		}
		pump(h, cl, 15000, [&] { return got.size() >= 60; });
		bool in_order = got.size() == 60;
		for (size_t i = 0; in_order && i < got.size(); ++i)
			in_order = got[i].size() == (i % 3 == 0 ? 3000u : 8u) && got[i][0] == (uint8_t)i;
		CHECK(in_order, "60 messages, some split, arrive once each and in order with 30% loss");
	}

	// 10. A refusal names its reason.
	{
		NetHost h;
		h.startHost(c);
		std::vector<std::unique_ptr<NetHost>> cl;
		cl.push_back(std::make_unique<NetHost>());
		cl[0]->setLocalPlayerName("late");
		cl[0]->startClient("127.0.0.1", h.localPort());
		pump(h, cl, 3000, [&] { return cl[0]->getLocalPeerId() != 0 && cl[0]->authResult() == ARC_OK; });
		h.refusePeer(cl[0]->getLocalPeerId(), NDR_SERVER_ALREADY_IN_GAME);
		pump(h, cl, 6000, [&] { return cl[0]->connectionLost(); });
		CHECK(cl[0]->connectionLost() && cl[0]->disconnectReason() == NDR_SERVER_ALREADY_IN_GAME,
		      "a refused late observer is told the game has started (reason 5)");
	}

	// 11. Every connect ACK lost: the client takes its id from the
	// authentication result, as 0.28's NetClient takes m_HostID.
	{
		NetHost h;
		h.startHost(c);
		h.set_drop_connect_acks(true);
		std::vector<std::unique_ptr<NetHost>> cl;
		for (int i = 0; i < 2; ++i) {
			cl.push_back(std::make_unique<NetHost>());
			cl.back()->setLocalPlayerName(i ? "dave" : "carol");
			cl.back()->startClient("127.0.0.1", h.localPort());
			pump(h, cl, 5000, [&] { return cl.back()->authResult() >= 0 || cl.back()->connectionLost(); });
		}
		CHECK(cl[0]->authResult() == ARC_OK && cl[1]->authResult() == ARC_OK && cl[0]->getLocalPeerId() == 1 &&
		      cl[1]->getLocalPeerId() == 2, "with every connect ACK lost, each joiner learns its id from the host's answer");
	}

	// 12. Names as the host keeps them.
	CHECK(NetHost::sanitise_player_name("  [b]bob[/b] ") == "{b}bob{/b}", "GUI tags are defused and the name trimmed");
	CHECK(NetHost::sanitise_player_name(std::string(40, 'x')) == std::string(32, 'x'), "at most 32 characters");
	CHECK(NetHost::sanitise_player_name(std::string(31, 'x') + "\xc3\xa9\xc3\xa9") == std::string(31, 'x') + "\xc3\xa9",
	      "counted in characters, not bytes");
	CHECK(NetHost::sanitise_player_name("   ") == "Anonymous", "a blank name is Anonymous");
	{
		NetHost h;
		h.setLocalPlayerName("host");
		h.setPassword("pw");
		h.setPasswordHasher([](const std::string &pw, const std::string &name) { return pw + "|" + name; });
		h.startHost(c);
		std::vector<std::string> in;
		h.onPeerConnected([&](uint32_t id) { in.push_back(h.getPeer(id)->player_name); });
		std::vector<std::unique_ptr<NetHost>> cl;
		cl.push_back(std::make_unique<NetHost>());
		cl[0]->setLocalPlayerName(" [color=red]x[/color] ");
		cl[0]->setPassword("pw");
		cl[0]->setPasswordHasher([](const std::string &pw, const std::string &name) { return pw + "|" + name; });
		cl[0]->startClient("127.0.0.1", h.localPort());
		pump(h, cl, 5000, [&] { return cl[0]->authResult() >= 0 || cl[0]->connectionLost(); });
		CHECK(cl[0]->authResult() == ARC_OK && in == std::vector<std::string>{"{color=red}x{/color}"},
		      "a joiner is admitted under the sanitised name, its password salted by the name it sent");
		cl[0]->stop();
	}

	// 11. Lobby authentication: tickets the toy lobby knows.
	{
		const std::map<std::string, std::string> lobby = {{"t-bob", "Bob"}, {"t-cy", "cy"}};
		NetHost h;
		h.setLocalPlayerName("host");
		std::vector<std::pair<uint32_t, std::string>> asked;
		h.setLobbyAuth([&](uint32_t peer, const std::string &ticket) { asked.push_back({peer, ticket}); });
		h.startHost(c);
		std::vector<std::string> in;
		h.onPeerConnected([&](uint32_t id) { in.push_back(h.getPeer(id)->player_name); });
		std::vector<std::unique_ptr<NetHost>> cl;
		auto lobby_join = [&](const std::string &name, const std::string &ticket, bool answer) {
			cl.push_back(std::make_unique<NetHost>());
			NetHost &j = *cl.back();
			j.setLocalPlayerName(name);
			j.setLobbyTicket(ticket);
			j.startClient("127.0.0.1", h.localPort());
			const size_t before = asked.size();
			pump(h, cl, 5000, [&] { return asked.size() > before || j.connectionLost(); });
			if (answer && asked.size() > before) {
				const auto it = lobby.find(asked.back().second);
				// A late answer about another ticket changes nothing.
				CHECK(!h.finishLobbyAuth(asked.back().first, "t-stale", "Bob"), "an answer about another ticket is ignored");
				h.finishLobbyAuth(asked.back().first, asked.back().second, it == lobby.end() ? "" : it->second);
			}
			pump(h, cl, 6000, [&] { return j.connectionLost() || j.authResult() >= 0; });
			return j.authResult() == ARC_OK ? -1 : j.disconnectReason();
		};
		CHECK(lobby_join("bob", "t-bob", true) == -1 && in == std::vector<std::string>{"bob"},
		      "bob's ticket names Bob: in, whatever the case");
		CHECK(lobby_join("cy (1500)", "t-cy", true) == -1 && in.size() == 2, "a lobby rating in the name is allowed for");
		CHECK(lobby_join("mallory", "t-bob", true) == NDR_LOBBY_AUTH_FAILED && in.size() == 2,
		      "someone else's ticket is refused (NDR_LOBBY_AUTH_FAILED)");
		CHECK(lobby_join("dan", "t-made-up", true) == NDR_LOBBY_AUTH_FAILED, "so is a ticket the lobby never gave");
		const size_t asked_before = asked.size();
		CHECK(lobby_join("eve", "", false) == NDR_LOBBY_AUTH_FAILED && asked.size() == asked_before,
		      "a direct join without a ticket gives up at the handshake (Join via lobby.)");
		lobby_join("fay", "t-cy", false);
		CHECK(in.size() == 2 && cl.back()->authResult() < 0 && !cl.back()->connectionLost(),
		      "a joiner waits while the lobby has not answered");
		for (auto &j : cl) j->stop();
	}

	if (failures) {
		std::printf("%d host check(s) failed\n", failures);
		return 1;
	}
	std::printf("all host checks passed\n");
	return 0;
}
