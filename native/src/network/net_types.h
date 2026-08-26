#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace rtso {
enum class NetRole { None, Host, Client };
struct NetConfig { uint16_t port = 7777; uint8_t maxPlayers = 8; };
struct NetPeer { uint32_t id; std::string address; uint16_t port; bool connected = false; };
}
