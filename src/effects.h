// effects.h - the list of things Chicken Control can do to the game.
//
// Each entry's id has to match the matching id in launcher/effects.py, or
// the app will send commands this mod doesn't recognise.
//
// Handlers run on the game's main thread (called from mod_update, never
// from the network callback), so they're safe to touch game state from.

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "game_api.h"
#include "mini_json.h"

namespace chickencontrol {

// One parsed line of the wire protocol:
//   {"cmd": "effect", "id": "player.give_rupees", "params": {"amount": 50}}
// `id` and `params` are all EffectRunner needs; `cmd` is only ever "effect"
// today and is checked by the caller before this is built.
struct EffectCommand {
    std::string id;
    JsonValue params;  // object JsonValue; use params.get("key").as_int(...) etc.
};

struct TimedEffect {
    std::string id;
    double remaining_seconds;
};

class EffectRunner {
public:
    using Handler = std::function<game::Result(const EffectCommand&)>;

    EffectRunner();

    // Runs the effect named in the command. Returns what happened, so the
    // log can say why something didn't work instead of staying silent.
    game::Result dispatch(const EffectCommand& cmd);

    // Called once a frame with how long the last frame took, so effects
    // that wear off can do that.
    void tick(double delta_seconds);

    // Ends everything still running. Called on shutdown so the game is
    // never left frozen or inverted.
    void endEverything();

    std::vector<std::string> activeEffects() const;

private:
    void registerHandlers();
    void startTimed(const std::string& id, double duration_seconds);
    void endTimed(const std::string& id);

    std::unordered_map<std::string, Handler> handlers_;
    std::vector<TimedEffect> active_timed_effects_;
};

}  // namespace chickencontrol
