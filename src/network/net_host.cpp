#include "net_host.h"
namespace rtso {
bool NetHost::startHost(const NetConfig& c) { cfg=c; role=NetRole::Host; return true; }
bool NetHost::startClient(const std::string& h, uint16_t p) { (void)h;(void)p; role=NetRole::Client; return true; }
void NetHost::poll(Arena& a) { (void)a; }
void NetHost::sendCommand(uint32_t peer, const std::vector<uint8_t>& d) { (void)peer;(void)d; }
void NetHost::onCommand(std::function<void(uint32_t,const uint8_t*,size_t)> cb){ (void)cb; }
void NetHost::stop(){ role=NetRole::None; }
}
