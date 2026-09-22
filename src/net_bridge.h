// net_bridge.h — the TCP side of the Chicken Control protocol, built on
// Dusklight's own NetService instead of a raw BSD socket.
//
// Unlike the old raw-socket version, everything here runs on the game
// thread: NetService is asynchronous and polled, so there is no background
// accept thread and no cross-thread queue. `poll()` is meant to be called
// once per mod_update() tick.
//
// Wire protocol (see launcher/mod_bridge.py): newline-delimited JSON.
//   -> {"cmd": "effect", "id": "player.give_rupees", "params": {"amount": 50}}
//   <- {"ok": true}
//   <- {"ok": false, "error": "unknown effect"}

#pragma once

#include "effects.h"

#include <mods/svc/net.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace chickencontrol {

class NetBridge {
public:
    NetBridge(std::string bind_host, uint16_t port);

    // Opens the listener. Returns false if NetService couldn't bind it
    // (e.g. the port is already in use by another instance of this mod).
    bool start();

    // Closes the listener and every open connection.
    void stop();

    // Drains NetService's event queue: accepts new connections, buffers
    // incoming bytes, splits them on '\n', parses each line as JSON,
    // dispatches it to `effects`, and writes the {"ok": ...} reply back on
    // the same connection. Call once per mod_update() tick.
    void poll(EffectRunner& effects);

private:
    struct Connection {
        mods::net::Socket socket;
        std::string recv_buffer;
    };

    void handleLine(const std::string& line, EffectRunner& effects, Connection& conn);
    void sendResponse(Connection& conn, bool ok, const std::string& error);

    std::string bind_host_;
    uint16_t port_;
    mods::net::Socket listener_;
    std::unordered_map<uint64_t, Connection> connections_;
};

}  // namespace chickencontrol
