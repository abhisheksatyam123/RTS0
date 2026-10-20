#ifndef RTSO_MUSIC_MANAGER_H
#define RTSO_MUSIC_MANAGER_H

// Port of 0 A.D. gui/common/music.js (GPL-2.0-or-later): menu, peace, battle,
// victory and defeat playlists, the civ's own peace tracks from
// simulation/data/civs/*.json "Music", and the default tracks music.js falls
// back to. The battle state comes from BattleDetection (attack_detection.h).
// Tracks are 0 A.D.'s audio/music/ (CC BY-SA 3.0) in assets/audio/music/.

#include <godot_cpp/classes/audio_stream_ogg_vorbis.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <random>
#include <string>
#include <vector>

namespace rtso {

// Loads an audio file, imported or not (assets/ isn't imported in headless runs).
inline godot::Ref<godot::AudioStream> load_audio(const godot::String &path) {
	using namespace godot;
	ResourceLoader *rl = ResourceLoader::get_singleton();
	if (rl && rl->exists(path)) {
		Ref<AudioStream> s = rl->load(path);
		if (s.is_valid()) return s;
	}
	if (path.ends_with(".ogg") && FileAccess::file_exists(path)) return AudioStreamOggVorbis::load_from_file(path);
	return Ref<AudioStream>();
}

// simulation/data/civs/<civ>.json "Music" (all "peace"), indexed by RTS0 civ id.
inline const std::vector<const char *> &civ_peace_tracks(uint8_t civ) {
	static const std::vector<const char *> table[16] = {
		{"Harvest_Festival.ogg", "Forging_a_City-State.ogg", "Highland_Mist.ogg", "The_Hellespont.ogg"}, // athen
		{"Helen_Leaves_Sparta.ogg", "Peaks_of_Atlas.ogg", "Forging_a_City-State.ogg", "The_Hellespont.ogg",
				"Only_300_Stood.ogg"}, // spart
		{"Eastern_Dreams.ogg", "Valley_of_the_Nile.ogg", "Land_between_the_two_Seas.ogg", "Sands_of_Time.ogg"}, // pers
		{"Juno_Protect_You.ogg", "Mediterranean_Waves.ogg", "Elysian_Fields.ogg", "The_Governor.ogg",
				"Alba_Longa_Burns.ogg", "Roman_Ingenuity.ogg"}, // rome
		{"Mediterranean_Waves.ogg", "Harsh_Lands_Rugged_People.ogg", "Peaks_of_Atlas.ogg", "From_Every_Corner.ogg"}, // cart
		{"Rise_of_Macedon.ogg", "In_the_Shadow_of_Olympus.ogg", "Elysian_Fields.ogg", "The_Hellespont.ogg",
				"Northern_Frontier.ogg", "Upstart_King.ogg", "Epitaph.ogg"}, // mace
		{"The_Fledgling_Kingdom.ogg", "Solstice_Festival.ogg", "Whistler.ogg", "As_Seasons_Change.ogg",
				"Highland_Mist.ogg", "Celtica.ogg", "Mountain_Idyll.ogg", "Albian_Nocturne.ogg"}, // brit
		{"Celtic_Pride.ogg", "Cisalpine_Gaul.ogg", "Harvest_Festival.ogg", "Water's_Edge.ogg", "Sunrise.ogg",
				"Harvest_Moon.ogg"}, // gaul
		{"An_old_Warhorse_goes_to_Pasture.ogg", "Celtica.ogg", "Harsh_Lands_Rugged_People.ogg", "Midwinter.ogg",
				"The_Pillars_of_Hercules.ogg"}, // iber
		{"An_old_Warhorse_goes_to_Pasture.ogg", "Land_between_the_two_Seas.ogg", "Eastern_Dreams.ogg",
				"Karmic_Confluence.ogg", "Beyond_The_Indus.ogg"}, // maur
		{"Ammon-Ra.ogg", "Sands_of_Time.ogg", "Land_between_the_two_Seas.ogg", "Valley_of_the_Nile.ogg",
				"Bandit_Country.ogg", "Mirage.ogg", "Point_of_No_Return.ogg"}, // kush
		{"Ammon-Ra.ogg", "Sands_of_Time.ogg", "Land_between_the_two_Seas.ogg", "Valley_of_the_Nile.ogg", "Epitaph.ogg",
				"Along_the_Nile.ogg"}, // ptol
		{"Rise_of_Macedon.ogg", "In_the_Shadow_of_Olympus.ogg", "The_Hellespont.ogg", "Cradle_of_Civilization.ogg",
				"Expedition.ogg", "Land_Between_Rivers.ogg"}, // sele
		{"Red_Dawn.ogg", "Hill_of_Sorrows.ogg"}, // han
		{}, // gaia
		{"Northern_Frontier.ogg", "As_Seasons_Change.ogg", "Epitaph.ogg", "From_Every_Corner.ogg", "Whistler.ogg"}, // germ
	};
	static const std::vector<const char *> none;
	return civ < 16 ? table[civ] : none;
}

class RtsoMusic {
public:
	enum State { OFF = 0, MENU, PEACE, BATTLE, VICTORY, DEFEAT, CUSTOM };

	static constexpr const char *kPath = "res://assets/audio/music/";
	// sound.musicgain 0.2 beside sound.actiongain 0.7 in 0.28; the buses are relative to
	// their defaults, so the music sits that ratio lower. (music.js musicGain is never read.)
	static constexpr float kGainDb = -10.5f;

	~RtsoMusic() {
		if (load_ && load_->thread.joinable()) load_->thread.join();
	}

	void build(godot::Node *parent) {
		for (auto &p : players_) {
			p = memnew(godot::AudioStreamPlayer);
			p->set_volume_db(-80.0f);
			p->set_bus("Music"); // sound.musicgain
			// 0 A.D.'s music is global, outside any page: it ignores the tree's
			// pause (the defeat question pauses a single-player game, and the
			// victory and defeat tracks must still start). Its tick is driven by
			// the MenuAudio autoload for the same reason.
			p->set_process_mode(godot::Node::PROCESS_MODE_ALWAYS);
			parent->add_child(p);
		}
		rng_.seed(std::random_device{}());
	}

	// Music.storeTracks(civMusic) + completeTracks(): the civ's peace list, and
	// the defaults for every state the civ leaves empty.
	void store_civ_tracks(uint8_t civ) {
		reset_tracks();
		const auto &civ_peace = civ_peace_tracks(civ);
		if (!civ_peace.empty()) tracks_[PEACE].assign(civ_peace.begin(), civ_peace.end());
	}

	void reset_tracks() {
		tracks_[MENU] = {"Honor_Bound.ogg", "An_old_Warhorse_goes_to_Pasture.ogg", "Calm_Before_the_Storm.ogg",
				"Juno_Protect_You.ogg"};
		std::shuffle(tracks_[MENU].begin() + 1, tracks_[MENU].end(), rng_);
		tracks_[PEACE] = {"Tale_of_Warriors.ogg", "Tavern_in_the_Mist.ogg", "The_Road_Ahead.ogg"};
		tracks_[BATTLE] = {"Taiko_1.ogg", "Taiko_2.ogg"};
		tracks_[VICTORY] = {"You_are_Victorious!.ogg"};
		tracks_[DEFEAT] = {"Dried_Tears.ogg"};
	}

	// Music.setState / updateState: a new state starts its (shuffled) playlist.
	void set_state(State s) {
		if (locked_ || s == state_) return;
		state_ = s;
		if (s == OFF) {
			fade_out_current(1.0);
			playlist_.clear();
			return;
		}
		playlist_ = tracks_[s];
		if (s != MENU) std::shuffle(playlist_.begin(), playlist_.end(), rng_);
		index_ = 0;
		// music.js fade-in periods: 3 s into peace, 2 s otherwise, none for the menu.
		start_track(s == PEACE ? 3.0 : s == MENU ? 0.0 : 2.0);
	}
	State state() const { return state_; }
	void set_locked(bool l) { locked_ = l; }
	// messages.js "play-tracks" (the "retro me" cheat): storeTracks as the custom list,
	// setState(CUSTOM), then locked so battles don't change it; empty unlocks.
	void play_custom(const std::vector<std::string> &tracks) {
		locked_ = false;
		if (!tracks.empty()) {
			tracks_[CUSTOM] = tracks;
			state_ = OFF; // restart even when already custom
			set_state(CUSTOM);
		}
		locked_ = !tracks.empty();
	}
	godot::String current_track() const { return current_name_; }
	// Playback position of the current track in seconds (tests, summary handoff).
	float current_position() const {
		godot::AudioStreamPlayer *cur = players_[cur_];
		return cur && cur->is_playing() ? (float)cur->get_playback_position() : 0.0f;
	}

	// Fades and moves to the next playlist item when a track ends (looping).
	void tick(double dt) {
		for (int i = 0; i < 2; ++i) {
			godot::AudioStreamPlayer *p = players_[i];
			if (!p) continue;
			float &g = gain_[i];
			if (fade_rate_[i] != 0.0) {
				g = (float)std::clamp(g + fade_rate_[i] * dt, 0.0, 1.0);
				if (g <= 0.0f && fade_rate_[i] < 0) {
					fade_rate_[i] = 0;
					p->stop();
				} else if (g >= 1.0f && fade_rate_[i] > 0) {
					fade_rate_[i] = 0;
				}
				p->set_volume_db(g > 0.001f ? kGainDb + 20.0f * std::log10(g) : -80.0f);
			}
		}
		poll_load();
		godot::AudioStreamPlayer *cur = players_[cur_];
		if (state_ != OFF && !load_ && cur && !playlist_.empty() && !cur->is_playing() && fade_rate_[cur_] >= 0) {
			index_ = (index_ + 1) % playlist_.size();
			start_track(0.0);
		}
	}

private:
	void fade_out_current(double secs) {
		if (players_[cur_] && players_[cur_]->is_playing())
			fade_rate_[cur_] = secs > 0 ? -gain_[cur_] / secs : -1e9;
	}

	// A track loads on its own thread (the decode setup cost ~35 ms of a frame on every
	// peace/battle switch); tick() starts it once loaded. A newer request supersedes one in
	// flight, which finishes and is dropped.
	struct Load {
		std::thread thread;
		std::atomic<bool> done{false};
		godot::Ref<godot::AudioStream> stream;
		godot::String path;
	};

	void start_track(double fade_in) {
		fade_out_current(1.0);
		want_fade_ = fade_in;
		tries_ = 0;
		++want_gen_;
		if (!load_) request_load();
	}

	void request_load() {
		if (playlist_.empty()) return;
		load_ = std::make_unique<Load>();
		load_->path = godot::String(kPath) + godot::String::utf8(playlist_[index_].c_str());
		load_gen_ = want_gen_;
		load_index_ = index_;
		Load *l = load_.get();
		l->thread = std::thread([l] {
			l->stream = load_audio(l->path);
			l->done.store(true, std::memory_order_release);
		});
	}

	void poll_load() {
		if (!load_ || !load_->done.load(std::memory_order_acquire)) return;
		load_->thread.join();
		godot::Ref<godot::AudioStream> s = load_->stream;
		const bool current = load_gen_ == want_gen_ && state_ != OFF;
		load_.reset();
		if (!current) { // superseded: load what is wanted now
			if (state_ != OFF) request_load();
			return;
		}
		if (s.is_valid()) {
			cur_ ^= 1;
			godot::AudioStreamPlayer *p = players_[cur_];
			if (!p) return;
			p->set_stream(s);
			gain_[cur_] = want_fade_ > 0 ? 0.0f : 1.0f;
			fade_rate_[cur_] = want_fade_ > 0 ? 1.0 / want_fade_ : 0.0;
			p->set_volume_db(want_fade_ > 0 ? -80.0f : kGainDb);
			p->play();
			current_name_ = godot::String::utf8(playlist_[load_index_].c_str());
			return;
		}
		// Skip tracks missing from this checkout, as Engine.AddPlaylistItem would.
		if (++tries_ < playlist_.size()) {
			index_ = (index_ + 1) % playlist_.size();
			request_load();
		}
	}

	std::unique_ptr<Load> load_;
	uint64_t want_gen_ = 0, load_gen_ = 0;
	size_t load_index_ = 0, tries_ = 0;
	double want_fade_ = 0.0;
	std::vector<std::string> tracks_[CUSTOM + 1];
	std::vector<std::string> playlist_;
	size_t index_ = 0;
	State state_ = OFF;
	bool locked_ = false;
	godot::AudioStreamPlayer *players_[2] = {nullptr, nullptr};
	float gain_[2] = {0, 0};
	double fade_rate_[2] = {0, 0};
	int cur_ = 0;
	godot::String current_name_;
	std::mt19937 rng_;
};

} // namespace rtso

#endif
