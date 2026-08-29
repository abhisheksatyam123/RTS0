#include "network/net_host.h"
#include "network/turn_manager.h"
#include "simulation/simulation.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace rtso;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Testing RTS0 Netcode & Multiplayer Transport" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Test Packet Serialization / Deserialization
    std::cout << "[Test 1] Message Framing & Serialization..." << std::endl;
    {
        // Handshake
        ClientHandshakeMsg chm;
        chm.engine_version = "RTS0-0.1-Test";
        chm.enabled_mods.push_back({"public", "0.0.26"});
        std::vector<uint8_t> chm_payload = NetPacketSerializer::frame_client_handshake(chm);
        ClientHandshakeMsg chm_parsed;
        [[maybe_unused]] bool ok = NetPacketSerializer::parse_client_handshake(chm_payload.data(), chm_payload.size(), chm_parsed);
        assert(ok);
        assert(chm_parsed.magic_response == PS_PROTOCOL_MAGIC_RESPONSE);
        assert(chm_parsed.engine_version == "RTS0-0.1-Test");
        assert(chm_parsed.enabled_mods.size() == 1);
        assert(chm_parsed.enabled_mods[0].first == "public");

        // Authenticate
        AuthenticateMsg am;
        am.name = "PlayerOne";
        am.password = "secret123";
        std::vector<uint8_t> am_payload = NetPacketSerializer::frame_authenticate(am);
        AuthenticateMsg am_parsed;
        ok = NetPacketSerializer::parse_authenticate(am_payload.data(), am_payload.size(), am_parsed);
        assert(ok);
        assert(am_parsed.name == "PlayerOne");
        assert(am_parsed.password == "secret123");

        // Ready
        ReadyMsg rm;
        rm.guid = "peer-1";
        rm.status = 1;
        std::vector<uint8_t> rm_payload = NetPacketSerializer::frame_ready(rm);
        ReadyMsg rm_parsed;
        ok = NetPacketSerializer::parse_ready(rm_payload.data(), rm_payload.size(), rm_parsed);
        assert(ok);
        assert(rm_parsed.guid == "peer-1");
        assert(rm_parsed.status == 1);

        // GameStart
        GameStartMsg gsm;
        gsm.init_attributes = "map=acropolis_bay;seed=42;civ=athenians";
        std::vector<uint8_t> gsm_payload = NetPacketSerializer::frame_game_start(gsm);
        GameStartMsg gsm_parsed;
        ok = NetPacketSerializer::parse_game_start(gsm_payload.data(), gsm_payload.size(), gsm_parsed);
        assert(ok);
        assert(gsm_parsed.init_attributes == "map=acropolis_bay;seed=42;civ=athenians");

        // SimulationCommand
        SimulationCommandMsg scm;
        scm.turn = 10;
        scm.client_id = 2;
        NetCommand cmd;
        cmd.client_id = 2;
        cmd.cmd_type = 1; // Move
        cmd.entity_ids = {101, 102, 103};
        cmd.target_x = 45.5f;
        cmd.target_z = 88.2f;
        scm.commands.push_back(cmd);
        std::vector<uint8_t> scm_payload = NetPacketSerializer::frame_simulation_command(scm);
        SimulationCommandMsg scm_parsed;
        ok = NetPacketSerializer::parse_simulation_command(scm_payload.data(), scm_payload.size(), scm_parsed);
        assert(ok);
        assert(scm_parsed.turn == 10);
        assert(scm_parsed.client_id == 2);
        assert(scm_parsed.commands.size() == 1);
        assert(scm_parsed.commands[0].entity_ids.size() == 3);
        assert(scm_parsed.commands[0].entity_ids[1] == 102);
        assert(scm_parsed.commands[0].target_x == 45.5f);

        // SyncCheck
        SyncCheckMsg sync;
        sync.turn = 10;
        sync.hash = 0xDEADBEEF;
        std::vector<uint8_t> sync_payload = NetPacketSerializer::frame_sync_check(sync);
        SyncCheckMsg sync_parsed;
        ok = NetPacketSerializer::parse_sync_check(sync_payload.data(), sync_payload.size(), sync_parsed);
        assert(ok);
        assert(sync_parsed.turn == 10);
        assert(sync_parsed.hash == 0xDEADBEEF);

        // Chat
        ChatMsg chat;
        chat.sender_guid = "peer-0";
        chat.message = "gg wp";
        std::vector<uint8_t> chat_payload = NetPacketSerializer::frame_chat(chat);
        ChatMsg chat_parsed;
        ok = NetPacketSerializer::parse_chat(chat_payload.data(), chat_payload.size(), chat_parsed);
        assert(ok);
        assert(chat_parsed.sender_guid == "peer-0");
        assert(chat_parsed.message == "gg wp");

        std::cout << "  [PASS] All 7 message types correctly framed & serialized." << std::endl;
    }

    // 2. Test Real Socket Loopback Transport
    std::cout << "[Test 2] Real UDP Socket Host/Client Loopback..." << std::endl;
    {
        NetConfig host_cfg;
        host_cfg.port = 20595; // Default 0 A.D. port
        NetHost host;
        [[maybe_unused]] bool host_started = host.startHost(host_cfg);
        assert(host_started);
        assert(host.isHost());

        NetHost client;
        [[maybe_unused]] bool client_started = client.startClient("127.0.0.1", 20595);
        assert(client_started);
        assert(client.isClient());

        Arena arena(65536);

        bool host_connected = false;
        bool client_connected = false;
        host.onPeerConnected([&](uint32_t peer_id) {
            host_connected = true;
            std::cout << "  Host saw peer connect, peer_id=" << peer_id << std::endl;
        });
        client.onPeerConnected([&](uint32_t peer_id) {
            client_connected = true;
            std::cout << "  Client saw connect ACK, local_peer_id=" << client.getLocalPeerId() << std::endl;
        });

        // Poll for connection handshake
        for (int i = 0; i < 50 && (!host_connected || !client_connected); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            host.poll(arena);
            client.poll(arena);
        }

        assert(host_connected);
        assert(client_connected);

        // Test sending reliable simulation command from client to host
        bool command_received = false;
        uint32_t received_peer = 0;
        std::vector<uint8_t> received_data;
        host.onCommand([&](uint32_t peer_id, const uint8_t *data, size_t size) {
            command_received = true;
            received_peer = peer_id;
            received_data.assign(data, data + size);
        });

        std::vector<uint8_t> test_payload = {0x11, 0x22, 0x33, 0x44};
        client.sendCommand(0, test_payload);

        for (int i = 0; i < 50 && !command_received; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            host.poll(arena);
            client.poll(arena);
        }

        assert(command_received);
        assert(received_data == test_payload);
        std::cout << "  [PASS] Real UDP socket host/client exchange verified." << std::endl;

        host.stop();
        client.stop();
    }

    // 3. Test TurnManager Multiplayer Synchronization & OOS Detection
    std::cout << "[Test 3] TurnManager Multiplayer Sync & OOS Detection..." << std::endl;
    {
        Simulation sim1;
        Simulation sim2;

        TurnManager tm1;
        TurnManager tm2;

        // Post identical commands to both
        NetCommand move_cmd;
        move_cmd.cmd_type = 1;
        move_cmd.entity_ids = {1};
        move_cmd.target_x = 10.0f;
        move_cmd.target_z = 10.0f;

        tm1.post_local_command(move_cmd);
        tm2.post_local_command(move_cmd);

        // Advance 1 turn
        [[maybe_unused]] bool t1 = tm1.tick(0.200f, sim1);
        [[maybe_unused]] bool t2 = tm2.tick(0.200f, sim2);
        assert(t1 && t2);
        assert(tm1.current_turn() == 1);
        assert(tm2.current_turn() == 1);

        uint32_t hash1 = tm1.compute_sim_hash(sim1);
        uint32_t hash2 = tm2.compute_sim_hash(sim2);
        assert(hash1 == hash2);
        std::cout << "  Synchronized turn hash: " << hash1 << " (both agree)" << std::endl;

        // Exchange hashes
        tm1.receive_sync_check(1, 2, hash2);
        assert(!tm1.has_out_of_sync());

        // Test out-of-sync trigger when peer sends divergent hash
        tm1.receive_sync_check(1, 3, 0x12345678);
        assert(tm1.has_out_of_sync());
        assert(tm1.oos_turn() == 1);
        assert(tm1.oos_expected_hash() == hash1);
        assert(tm1.oos_received_hash() == 0x12345678);
        std::cout << "  [PASS] Out-of-sync detection triggered correctly." << std::endl;
    }

    // 4. Dual-sim lockstep hashes after several empty turns (HashSerializer after sim ticks)
    std::cout << "[Test 4] Dual Simulation lockstep hash agreement..." << std::endl;
    {
        Simulation a;
        Simulation b;
        a.reset();
        b.reset();
        TurnManager ta;
        TurnManager tb;
        for (int t = 0; t < 8; ++t) {
            [[maybe_unused]] bool ok_a = ta.tick(TurnManager::TURN_DURATION, a);
            [[maybe_unused]] bool ok_b = tb.tick(TurnManager::TURN_DURATION, b);
            assert(ok_a && ok_b);
            uint32_t ha = ta.compute_sim_hash(a);
            uint32_t hb = tb.compute_sim_hash(b);
            assert(ha == hb);
        }
        assert(ta.current_turn() == 8);
        std::cout << "  [PASS] 8 empty turns hashed identically on both sims." << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "ALL NETCODE TESTS PASSED (100%)" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
