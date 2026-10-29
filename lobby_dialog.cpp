#include "lobby_dialog.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void RtsoLobbyDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("host_requested", PropertyInfo(Variant::STRING, "server_name"), PropertyInfo(Variant::INT, "port")));
	ADD_SIGNAL(MethodInfo("join_requested", PropertyInfo(Variant::STRING, "ip_address"), PropertyInfo(Variant::INT, "port")));
	ADD_SIGNAL(MethodInfo("start_game_requested"));
	ADD_SIGNAL(MethodInfo("map_selected", PropertyInfo(Variant::STRING, "map_name")));
	ADD_SIGNAL(MethodInfo("civ_selected", PropertyInfo(Variant::STRING, "civ_name")));
	ADD_SIGNAL(MethodInfo("lobby_closed"));

	ClassDB::bind_method(D_METHOD("request_host", "server_name", "port"), &RtsoLobbyDialog::request_host);
	ClassDB::bind_method(D_METHOD("request_join", "ip_address", "port"), &RtsoLobbyDialog::request_join);
	ClassDB::bind_method(D_METHOD("request_start_game"), &RtsoLobbyDialog::request_start_game);
	ClassDB::bind_method(D_METHOD("select_map", "map_name"), &RtsoLobbyDialog::select_map);
	ClassDB::bind_method(D_METHOD("select_civ", "civ_name"), &RtsoLobbyDialog::select_civ);
	ClassDB::bind_method(D_METHOD("close_lobby"), &RtsoLobbyDialog::close_lobby);
}

RtsoLobbyDialog::RtsoLobbyDialog() {}

RtsoLobbyDialog::~RtsoLobbyDialog() {}

void RtsoLobbyDialog::request_host(const String &server_name, int port) {
	emit_signal("host_requested", server_name, port);
}

void RtsoLobbyDialog::request_join(const String &ip_address, int port) {
	emit_signal("join_requested", ip_address, port);
}

void RtsoLobbyDialog::request_start_game() {
	emit_signal("start_game_requested");
}

void RtsoLobbyDialog::select_map(const String &map_name) {
	emit_signal("map_selected", map_name);
}

void RtsoLobbyDialog::select_civ(const String &civ_name) {
	emit_signal("civ_selected", civ_name);
}

void RtsoLobbyDialog::close_lobby() {
	emit_signal("lobby_closed");
}

} // namespace godot
