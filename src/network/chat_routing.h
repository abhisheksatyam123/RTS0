#pragma once
// 0 A.D. ChatAddressees.js isAddressee, shared by the host's relay (NetServer
// OnChat sends a line only to its addressees) and the receiver's own check, so
// the two cannot drift apart.
#include <cstdint>
#include <string>

namespace rtso {

// The addressee tag a chat line travels with on the wire ("[Allies]",
// "[Enemies]", "[Observers]", "[Player-N]", "[Flare]"), or "" for everyone.
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
	return std::string();
}

// Whether a line tagged `tag` from player `from` reaches player `to`. Players
// at or above max_players are observers; "[Player-N]" names player id + 1, 0
// for an observer. ally(a, b) / enemy(a, b): a's stance toward b.
template <class Ally, class Enemy>
bool chat_tag_reaches(uint8_t from, uint8_t to, const std::string &tag, uint8_t max_players,
		Ally &&ally, Enemy &&enemy) {
	const bool from_obs = from >= max_players, to_obs = to >= max_players;
	if (tag.empty()) return true;
	if (tag == "[Observers]") return to_obs;
	if (tag == "[Allies]") return !from_obs && !to_obs && ally(from, to) && ally(to, from);
	if (tag == "[Enemies]") return !from_obs && !to_obs && enemy(from, to);
	// 0 A.D. sends a flare to the sender's mutual allies and the observers.
	if (tag == "[Flare]") return to_obs || (!from_obs && ally(from, to) && ally(to, from));
	if (tag.compare(0, 8, "[Player-") == 0) {
		int target = 0;
		for (size_t i = 8; i + 1 < tag.size(); ++i) {
			if (tag[i] < '0' || tag[i] > '9' || i >= 11) return false;
			target = target * 10 + (tag[i] - '0');
		}
		// No private message from an observer to a player.
		return (to_obs ? target == 0 : target == (int)to + 1) && (!from_obs || to_obs);
	}
	return true;
}

} // namespace rtso
