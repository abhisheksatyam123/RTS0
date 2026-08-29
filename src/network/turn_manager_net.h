#pragma once
#include "../simulation/system/turn_manager.h"
#include "net_host.h"
namespace rtso {
class NetTurnManager : public TurnManager {
    NetHost* net = nullptr;
public:
    void attach(NetHost* n){ net=n; }
    void submitCommands(uint32_t turn, const std::vector<uint8_t>& cmds) override;
    bool collectTurn(uint32_t turn, std::vector<uint8_t>& out) override;
};
}
