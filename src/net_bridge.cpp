#include "net_bridge.h"

#include <mods/svc/log.h>

#include <span>
#include <stdexcept>

namespace chickencontrol {

namespace {
// Minimal escaping for the one place this mod writes JSON by hand: the
// `error` string in a failure reply. Effect ids and detail messages are
// all plain ASCII written by this codebase, but escape defensively anyway
// since detail strings can echo back things like an unrecognised id.
std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Skip other control characters rather than emit invalid JSON.
                } else {
                    out += c;
                }
        }
    }
    return out;
}
}  // namespace

NetBridge::NetBridge(std::string bind_host, uint16_t port)
    : bind_host_(std::move(bind_host)), port_(port) {}

bool NetBridge::start() {
    const std::string endpoint = "tcp://" + bind_host_ + ":" + std::to_string(port_);
    mods::net::BindOutcome bound;
    listener_ = mods::net::listen(endpoint, &bound);
    if (!listener_) {
        svc_log->error(mod_ctx, ("chicken-control: failed to listen on " + endpoint).c_str());
        return false;
    }
    return true;
}

void NetBridge::stop() {
    connections_.clear();
    listener_.close();
}

void NetBridge::poll(EffectRunner& effects) {
    if (!listener_) return;

    mods::net::Event event;
    while (mods::net::poll(event)) {
        switch (event.type) {
            case NET_EVENT_ACCEPTED: {
                if (event.accepted == 0) break;
                connections_.emplace(event.accepted,
                                      Connection{mods::net::adopt(event.accepted), std::string()});
                break;
            }
            case NET_EVENT_STREAM_DATA: {
                auto it = connections_.find(event.handle);
                if (it == connections_.end()) break;
                Connection& conn = it->second;
                if (!event.data.empty()) {
                    conn.recv_buffer.append(reinterpret_cast<const char*>(event.data.data()),
                                             event.data.size());
                }

                // Split off every complete line currently buffered. A
                // command that spans multiple TCP packets simply waits
                // here until its '\n' arrives.
                size_t newline;
                while ((newline = conn.recv_buffer.find('\n')) != std::string::npos) {
                    std::string line = conn.recv_buffer.substr(0, newline);
                    conn.recv_buffer.erase(0, newline + 1);
                    if (!line.empty()) {
                        handleLine(line, effects, conn);
                    }
                }
                break;
            }
            case NET_EVENT_CLOSED: {
                connections_.erase(event.handle);
                break;
            }
            default:
                break;
        }
    }
}

void NetBridge::handleLine(const std::string& line, EffectRunner& effects, Connection& conn) {
    EffectCommand cmd;
    std::string parse_error;
    bool parsed_ok = true;

    try {
        JsonValue msg = JsonParser::parse(line);
        cmd.id = msg.get("id").as_string();
        cmd.params = msg.get("params");
        if (cmd.id.empty()) {
            parsed_ok = false;
            parse_error = "missing 'id'";
        }
    } catch (const std::exception& e) {
        parsed_ok = false;
        parse_error = std::string("bad json: ") + e.what();
    }

    if (!parsed_ok) {
        sendResponse(conn, false, parse_error);
        return;
    }

    game::Result result = effects.dispatch(cmd);
    sendResponse(conn, result.ok, result.detail);
}

void NetBridge::sendResponse(Connection& conn, bool ok, const std::string& error) {
    std::string payload = ok ? "{\"ok\":true}\n"
                              : "{\"ok\":false,\"error\":\"" + escapeJson(error) + "\"}\n";
    std::span<const char> chars(payload.data(), payload.size());
    conn.socket.send(std::as_bytes(chars));
}

}  // namespace chickencontrol
