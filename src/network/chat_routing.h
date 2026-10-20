#pragma once
// 0 A.D. ChatAddressees.js isAddressee, shared by the host's relay (NetServer
// OnChat sends a line only to its addressees) and the receiver's own check, so
// the two cannot drift apart.
#include <cstdint>
#include <string>

namespace rtso {

// The addressee tag a chat line travels with on the wire ("[Allies]",
// "[Enemies]", "[Observers]", "[Player-N]", "[Observer-C]", "[Flare]"), or ""
// for everyone.
// "[Team]", from older senders, means "[Allies]".
inline std::string chat_wire_tag(const std::string &msg) {
	if (msg.empty() || msg[0] != '[') return std::string();
	const size_t close = msg.find(']');
	if (close == std::string::npos) return std::string();
	const std::string tag = msg.substr(0, close + 1);
	if (tag == "[Team]") return "[Allies]";
	if (tag == "[Allies]" || tag == "[Enemies]" || tag == "[Observers]" || tag == "[Flare]") return tag;
	if (tag.size() > 9 && tag.size() <= 12 && tag.compare(0, 8, "[Player-") == 0 &&
			tag.find_first_not_of("0123456789", 8) == tag.size() - 1)
		return tag;
	if (tag.size() > 11 && tag.size() <= 21 && tag.compare(0, 10, "[Observer-") == 0 &&
			tag.find_first_not_of("0123456789", 10) == tag.size() - 1)
		return tag;
	return std::string();
}

// The client id a "[Observer-C]" tag names, or false for any other tag.
inline bool chat_observer_client(const std::string &tag, uint32_t &client) {
	if (tag.size() <= 11 || tag.size() > 21 || tag.compare(0, 10, "[Observer-") != 0 || tag.back() != ']') return false;
	uint64_t c = 0;
	for (size_t i = 10; i + 1 < tag.size(); ++i) {
		if (tag[i] < '0' || tag[i] > '9') return false;
		c = c * 10 + (uint64_t)(tag[i] - '0');
	}
	if (c > UINT32_MAX) return false;
	client = (uint32_t)c;
	return true;
}

// Whether a line tagged `tag` from player `from` reaches player `to`. Players
// at or above max_players are observers; "[Player-N]" names player id + 1, 0
// for an observer (any observer: older senders). "[Observer-C]" names one
// observer by its client id, as 0.28's /msg names a GUID: to_client is the
// receiver's. ally(a, b) / enemy(a, b): a's stance toward b. from_done /
// to_done: that player has won or been defeated, so it counts as an observer
// for observer chat, flares and private messages (0.28 isPlayerObserver:
// state != "active"), while Allies and Enemies still go by its seat (g_Players).
template <class Ally, class Enemy>
bool chat_tag_reaches(uint8_t from, uint8_t to, const std::string &tag, uint8_t max_players,
		Ally &&ally, Enemy &&enemy, uint32_t to_client = UINT32_MAX, bool from_done = false, bool to_done = false) {
	const bool from_seat = from < max_players, to_seat = to < max_players;
	const bool from_obs = !from_seat || from_done, to_obs = !to_seat || to_done;
	if (tag.empty()) return true;
	if (tag == "[Observers]") return to_obs;
	if (tag == "[Allies]") return from_seat && to_seat && ally(from, to) && ally(to, from);
	if (tag == "[Enemies]") return from_seat && to_seat && enemy(from, to);
	// 0 A.D. shows a flare to the observers and the sender's mutual allies.
	if (tag == "[Flare]") return to_obs || (from_seat && ally(from, to) && ally(to, from));
	uint32_t observer = 0;
	if (chat_observer_client(tag, observer)) return !to_seat && to_client == observer;
	if (tag.compare(0, 8, "[Player-") == 0) {
		int target = 0;
		for (size_t i = 8; i + 1 < tag.size(); ++i) {
			if (tag[i] < '0' || tag[i] > '9' || i >= 11) return false;
			target = target * 10 + (tag[i] - '0');
		}
		// No private message from an observer to a player.
		return (to_seat ? target == (int)to + 1 : target == 0) && (!from_obs || to_obs);
	}
	return true;
}

} // namespace rtso
