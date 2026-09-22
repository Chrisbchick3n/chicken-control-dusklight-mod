// mod.cpp — the Dusklight native mod entry point for Chicken Control.
//
// This is a real Dusklight mod (see docs/modding.md): it exports
// mod_initialize/mod_update/mod_shutdown, imports LogService and
// NetService, and is loaded by Dusklight's mod loader from a `.dusk`
// bundle rather than being launched as its own process.

#include "mods/service.hpp"
#include "mods/svc/log.h"
#include "mods/svc/net.hpp"

#include "effects.h"
#include "net_bridge.h"

#include <chrono>
#include <memory>

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(NetService, svc_net);

namespace {

// Must match launcher/config.py's AppConfig.mod_port default.
constexpr const char* kBindHost = "127.0.0.1";
constexpr uint16_t kBindPort = 47201;

std::unique_ptr<chickencontrol::NetBridge> g_bridge;
std::unique_ptr<chickencontrol::EffectRunner> g_effects;
std::chrono::steady_clock::time_point g_last_tick;

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    g_effects = std::make_unique<chickencontrol::EffectRunner>();
    g_bridge = std::make_unique<chickencontrol::NetBridge>(kBindHost, kBindPort);

    if (!g_bridge->start()) {
        g_bridge.reset();
        g_effects.reset();
        return mods::set_error(error, MOD_ERROR,
                                "chicken-control: could not open the TCP listener");
    }

    svc_log->info(mod_ctx, "chicken-control: listening on 127.0.0.1:47201");
    g_last_tick = std::chrono::steady_clock::now();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (!g_bridge || !g_effects) return MOD_OK;

    auto now = std::chrono::steady_clock::now();
    const double delta = std::chrono::duration<double>(now - g_last_tick).count();
    g_last_tick = now;

    // Accept/parse/dispatch anything received since the last tick.
    g_bridge->poll(*g_effects);

    // Advance timed effects (freeze/invert) and re-apply their input
    // overrides every frame while they're active.
    g_effects->tick(delta);

    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    // Never leave the player frozen or inverted just because the mod was
    // disabled or reloaded mid-effect.
    if (g_effects) g_effects->endEverything();
    if (g_bridge) g_bridge->stop();

    g_bridge.reset();
    g_effects.reset();

    svc_log->info(mod_ctx, "chicken-control: shut down");
    return MOD_OK;
}

}  // extern "C"
