#ifndef RTSO_HOTKEY_CONFIG_H
#define RTSO_HOTKEY_CONFIG_H

// Script access to the hotkey table for the hotkey editor (port of 0 A.D.
// gui/hotkeys/HotkeysPage.js and HotkeyPicker.js; the engine calls they use,
// Engine.GetHotkeyMap / GetConflicts / ConfigDB_* / ReloadHotkeys, map onto
// the methods below). Bindings are saved to user://config/user.cfg as
// "hotkey.<name> = ..." lines, only where they differ from default.cfg.

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

class RtsoHotkeyConfig : public RefCounted {
	GDCLASS(RtsoHotkeyConfig, RefCounted)

protected:
	static void _bind_methods();

public:
	// Engine.GetHotkeyMap(): name -> PackedStringArray of combinations ("Ctrl+F5").
	Dictionary get_hotkey_map() const;
	// default.cfg's combinations for one hotkey.
	PackedStringArray get_default(const String &name) const;
	// Replaces one hotkey's combinations in the live table (not saved yet).
	void set_hotkey(const String &name, const PackedStringArray &combos);
	// Engine.GetConflicts(combination): every hotkey on that key set.
	PackedStringArray get_conflicts(const String &combo) const;
	// Writes the user.cfg diff. Returns false when the file can't be written.
	bool save_user() const;
	// Drops user.cfg and goes back to default.cfg (0 A.D. "Reset").
	void reset_user();
	// Re-reads default.cfg, then user.cfg (Engine.ReloadHotkeys).
	void reload();
	// Canonical key name of a key or mouse-button event ("ctrl", "f5", "mouseleft"); "" if none.
	String event_key_name(const Ref<InputEvent> &event) const;
	PackedStringArray fired(const Ref<InputEvent> &event) const;
	// Formats held keys as a combination, modifiers first ("Ctrl+Shift+A").
	String format_keys(const PackedStringArray &keys) const;
	String display(const String &name) const;
	String display_combo(const String &combo) const;
	// Engine.HotkeyIsPressed(name): every key of one of its combinations is down.
	bool is_held(const String &name) const;
};

#endif
