#pragma once
// 0 A.D. simulation/ai/petra/chatHelper.js: the lines a Petra AI says, English only
// (0 A.D. marks them for translation). A PetraChatLine names its kind and variant
// (pickRandom) and the players, resource and amount; petra_chat_text fills them in.

#include "petra_ai.h"

#include <string>

namespace rtso {

struct PetraChatPool {
	const char *const *lines = nullptr;
	uint8_t count = 0;
};

namespace petra_chat_detail {
template <size_t N>
constexpr PetraChatPool pool(const char *const (&lines)[N]) { return PetraChatPool{lines, (uint8_t)N}; }

inline constexpr const char *kAttackHuge[] = {
	"I am starting a massive military campaign against %(_player_)s, come and join me.",
	"I have set up a huge army to crush %(_player_)s. Join me and you will have your share of the loot.",
	"I am launching an unprecedented attack against %(_player_)s. Bring your armies and we may soon win this war.",
};
inline constexpr const char *kAttackOther[] = {
	"I am launching an attack against %(_player_)s.",
	"I have just sent an army against %(_player_)s.",
	"I am sending an army against %(_player_)s.",
};
inline constexpr const char *kAnswerAttackJoin[] = {
	"Let me regroup my army and I will then join you against %(_player_)s.",
	"I am finishing preparations to attack %(_player_)s.",
};
inline constexpr const char *kAnswerAttackDecline[] = {
	"Sorry, I do not have enough soldiers currently; but my next attack will target %(_player_)s.",
	"Sorry, I still need to strengthen my army. However, I will attack %(_player_)s next.",
};
inline constexpr const char *kAnswerAttackOther[] = {
	"I cannot help you against %(_player_)s for the time being, I am planning to attack %(_player_2)s first.",
};
// Commands.js "attack-request": the notice the asking player's allies get.
inline constexpr const char *kAttackRequested[] = { "Attack against %(_player_)s requested." };
inline constexpr const char *kSentTribute[] = {
	"Here is a gift for you, %(_player_)s. Make good use of it.",
	"I see you are in a bad situation, %(_player_)s. I hope this helps.",
	"I can help you this time, %(_player_)s, but you should manage your resources more carefully in the future.",
};
inline constexpr const char *kRequestTribute[] = {
	"I am in need of %(resource)s, can you help? I will make it up to you.",
	"I would participate more efficiently in our common war effort if you could provide me some %(resource)s.",
	"If you can spare me some %(resource)s, I will be able to strengthen my army.",
};
inline constexpr const char *kNewTradeRoute[] = {
	"I have set up a new route with %(_player_)s. Trading will be profitable for all of us.",
	"A new trade route is set up with %(_player_)s. Take your share of the profits.",
};
inline constexpr const char *kNewAlly[] = { "%(_player_)s and I are now allies." };
inline constexpr const char *kNewNeutral[] = { "%(_player_)s and I are now neutral." };
inline constexpr const char *kNewEnemy[] = { "%(_player_)s and I are now enemies." };

inline constexpr const char *kAllyDecline[] = {
	"I cannot accept your offer to become allies, %(_player_)s.",
	"I must decline your offer of alliance, %(_player_)s.",
	"An alliance with you is not in my best interest, %(_player_)s.",
};
inline constexpr const char *kAllyDeclineSuggestNeutral[] = {
	"I will not be your ally, %(_player_)s. However, I will consider a neutrality pact.",
	"I reject your request for alliance, %(_player_)s, but we could become neutral.",
	"%(_player_)s, only a neutrality agreement is conceivable to me.",
};
inline constexpr const char *kAllyDeclineRepeated[] = {
	"Our previous alliance did not work out, %(_player_)s. I must decline your offer.",
	"I won’t ally you again, %(_player_)s!",
	"No more alliances between us, %(_player_)s!",
	"Your request for peace means nothing to me anymore, %(_player_)s!",
	"My answer to your repeated peace proposal will remain war, %(_player_)s!",
};
inline constexpr const char *kAllyAccept[] = {
	"I will accept your offer to become allies, %(_player_)s. We will both benefit from this partnership.",
	"An alliance between us is a good idea, %(_player_)s.",
	"Let both of our people prosper from a peaceful association, %(_player_)s.",
	"We have found common ground, %(_player_)s. I accept the alliance.",
	"%(_player_)s, consider us allies from now on.",
};
inline constexpr const char *kAllyAcceptWithTribute[] = {
	"I will ally with you, %(_player_)s, but only if you send me a tribute of %(_amount_)s %(_resource_)s.",
	"%(_player_)s, you must send me a tribute of %(_amount_)s %(_resource_)s before I accept an alliance with you.",
	"Unless you send me %(_amount_)s %(_resource_)s, an alliance won’t be formed, %(_player_)s.",
};
inline constexpr const char *kAllyWaitingForTribute[] = {
	"%(_player_)s, my offer still stands. I will ally with you only if you send me a tribute of %(_amount_)s %(_resource_)s.",
	"I’m still waiting for %(_amount_)s %(_resource_)s before accepting your alliance, %(_player_)s.",
	"%(_player_)s, if you do not send me part of the %(_amount_)s %(_resource_)s tribute soon, I will break off our negotiations.",
};
inline constexpr const char *kNeutralDecline[] = {
	"I will not become neutral with you, %(_player_)s.",
	"%(_player_)s, I must decline your request for a neutrality pact.",
};
inline constexpr const char *kNeutralDeclineRepeated[] = {
	"Our previous neutrality agreement ended in failure, %(_player_)s; I will not consider another one.",
};
inline constexpr const char *kNeutralAccept[] = {
	"I welcome your request for peace between our civilizations, %(_player_)s. I will accept.",
	"%(_player_)s, I will accept your neutrality request. May both our civilizations benefit.",
};
inline constexpr const char *kNeutralAcceptWithTribute[] = {
	"If you send me a tribute of %(_amount_)s %(_resource_)s, I will accept your neutrality request, %(_player_)s.",
	"%(_player_)s, if you send me %(_amount_)s %(_resource_)s, I will accept a neutrality pact.",
};
inline constexpr const char *kNeutralWaitingForTribute[] = {
	"%(_player_)s, I will not accept your neutrality request unless you tribute me %(_amount_)s %(_resource_)s soon.",
	"%(_player_)s, if you do not send me part of the %(_amount_)s %(_resource_)s tribute soon, I will break off our negotiations.",
};
inline constexpr const char *kAllySendRequest[] = {
	"%(_player_)s, it would help both of our civilizations if we formed an alliance. If you become allies with me, I will respond in kind.",
};
inline constexpr const char *kAllyRequestExpired[] = {
	"%(_player_)s, my offer for an alliance has expired.",
	"%(_player_)s, I have rescinded my previous offer for an alliance between us.",
};
inline constexpr const char *kNeutralSendRequest[] = {
	"%(_player_)s, I would like to request a neutrality pact between our civilizations. If you become neutral with me, I will respond in kind.",
	"%(_player_)s, it would be both to our benefit if we negotiated a neutrality pact. I will become neutral with you if you do the same.",
};
inline constexpr const char *kNeutralRequestExpired[] = {
	"%(_player_)s, I have decided to revoke my offer for a neutrality pact.",
	"%(_player_)s, as you have failed to respond to my request for peace between us, I have abrogated my offer.",
};
inline constexpr const char *kEmergencyEnter[] = {
	"My armies failed while defending my empire. Please honor our alliance and send help!",
	"My humble armies feel weak and tired. My civilization depends on our alliance, please send help!",
	"I need your help. Please do not let my armies fall!",
};
inline constexpr const char *kEmergencyExit[] = {
	"My empire regained its old strength, now it is time to seek revenge together!",
	"My civilization raised from the ashes. Now my armies feel stronger and eager to fight for our alliance.",
};

inline void replace_all(std::string &s, const char *key, const std::string &value) {
	const std::string k(key);
	for (size_t at = s.find(k); at != std::string::npos; at = s.find(k, at + value.size())) s.replace(at, k.size(), value);
}
} // namespace petra_chat_detail

// The lines one kind picks from (sub: request type 0 ally / 1 neutral, or PetraChatStance).
inline PetraChatPool petra_chat_pool(uint8_t kind, uint8_t sub) {
	using namespace petra_chat_detail;
	const bool neutral = sub == 1;
	switch (kind) {
		case PETRA_CHAT_ATTACK: return pool(kAttackOther);
		case PETRA_CHAT_HUGE_ATTACK: return pool(kAttackHuge);
		case PETRA_CHAT_ATTACK_JOIN: return pool(kAnswerAttackJoin);
		case PETRA_CHAT_ATTACK_DECLINE: return pool(kAnswerAttackDecline);
		case PETRA_CHAT_ATTACK_OTHER: return pool(kAnswerAttackOther);
		case PETRA_CHAT_TRIBUTE: return pool(kSentTribute);
		case PETRA_CHAT_TRIBUTE_REQUEST: return pool(kRequestTribute);
		case PETRA_CHAT_TRADE_ROUTE: return pool(kNewTradeRoute);
		case PETRA_CHAT_NEW_DIPLOMACY:
			return sub == PETRA_STANCE_ALLY ? pool(kNewAlly) : sub == PETRA_STANCE_NEUTRAL ? pool(kNewNeutral) : pool(kNewEnemy);
		case PETRA_CHAT_ANSWER_DECLINE: return neutral ? pool(kNeutralDecline) : pool(kAllyDecline);
		// 0 A.D. suggests neutrality only to a would-be ally.
		case PETRA_CHAT_ANSWER_DECLINE_SUGGEST_NEUTRAL: return neutral ? pool(kNeutralDecline) : pool(kAllyDeclineSuggestNeutral);
		case PETRA_CHAT_ANSWER_DECLINE_REPEATED: return neutral ? pool(kNeutralDeclineRepeated) : pool(kAllyDeclineRepeated);
		case PETRA_CHAT_ANSWER_ACCEPT: return neutral ? pool(kNeutralAccept) : pool(kAllyAccept);
		case PETRA_CHAT_ANSWER_ACCEPT_WITH_TRIBUTE: return neutral ? pool(kNeutralAcceptWithTribute) : pool(kAllyAcceptWithTribute);
		case PETRA_CHAT_ANSWER_WAITING_FOR_TRIBUTE: return neutral ? pool(kNeutralWaitingForTribute) : pool(kAllyWaitingForTribute);
		case PETRA_CHAT_SEND_REQUEST: return neutral ? pool(kNeutralSendRequest) : pool(kAllySendRequest);
		case PETRA_CHAT_REQUEST_EXPIRED: return neutral ? pool(kNeutralRequestExpired) : pool(kAllyRequestExpired);
		case PETRA_CHAT_EMERGENCY: return pool(kEmergencyEnter);
		case PETRA_CHAT_EMERGENCY_EXIT: return pool(kEmergencyExit);
		case PETRA_CHAT_ATTACK_REQUESTED: return pool(kAttackRequested);
		default: return {};
	}
}

// Resources.GetNames() in a sentence ("withinSentence": lower case), by ResourceType.
inline const char *petra_chat_resource_name(uint8_t res) {
	static const char *kNames[] = { "wood", "metal", "stone", "food" };
	return res < 4 ? kNames[res] : "resources";
}

// The line's text, with player_name(pid) for %(_player_)s and %(_player_2)s; translate(msgid)
// gives the line and the resource name in the player's language first (chatHelper.js sends
// each line with "translateMessage", and the resource "translateParameters").
template <class NameFn, class TranslateFn>
std::string petra_chat_text(const PetraChatLine &l, NameFn &&player_name, TranslateFn &&translate) {
	const PetraChatPool p = petra_chat_pool(l.kind, l.sub);
	if (!p.count) return std::string();
	std::string s = translate(p.lines[l.variant % p.count]);
	const std::string res = translate(petra_chat_resource_name(l.resource));
	petra_chat_detail::replace_all(s, "%(_player_2)s", l.player2 < 0xFE ? std::string(player_name(l.player2)) : std::string());
	petra_chat_detail::replace_all(s, "%(_player_)s", l.player < 0xFE ? std::string(player_name(l.player)) : std::string());
	petra_chat_detail::replace_all(s, "%(_amount_)s", std::to_string(l.amount));
	petra_chat_detail::replace_all(s, "%(_resource_)s", res);
	petra_chat_detail::replace_all(s, "%(resource)s", res);
	return s;
}

template <class NameFn>
std::string petra_chat_text(const PetraChatLine &l, NameFn &&player_name) {
	return petra_chat_text(l, player_name, [](const char *msgid) { return std::string(msgid); });
}

} // namespace rtso
