#pragma once
#include "net_types.h"
#include "../core/memory/arena.h"
#include <functional>
namespace rtso {
class NetHost {
    NetConfig cfg; NetRole role = NetRole::None;
public:
    bool startHost(const NetConfig& c);
    bool startClient(const std::string& host, uint16_t port);
    void poll(Arena& arena);
    void sendCommand(uint32_t peer, const std::vector<uint8_t>& data);
    void onCommand(std::function<void(uint32_t, const uint8_t*, size_t)> cb);
    void stop();
};
}
