#include "presentation/chat_window.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void RtsoChatWindow::_bind_methods() {
	ADD_SIGNAL(MethodInfo("chat_submitted", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("message_submitted", PropertyInfo(Variant::STRING, "message"), PropertyInfo(Variant::INT, "channel")));
	ADD_SIGNAL(MethodInfo("chat_visibility_changed", PropertyInfo(Variant::BOOL, "visible")));
	ADD_SIGNAL(MethodInfo("channel_changed", PropertyInfo(Variant::INT, "channel")));
}
