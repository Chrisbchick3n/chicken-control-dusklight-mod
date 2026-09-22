#include "effects.h"

#include <algorithm>
#include <iostream>

namespace chickencontrol {

namespace {
constexpr int kQuartersPerHeart = 4;
constexpr double kFreezeSeconds = 8.0;
constexpr double kInvertSeconds = 15.0;

int paramInt(const EffectCommand& cmd, const char* key, int fallback) {
    return cmd.params.get(key).as_int(fallback);
}

std::string paramString(const EffectCommand& cmd, const char* key, const char* fallback) {
    return cmd.params.get(key).as_string(fallback);
}
}  // namespace

EffectRunner::EffectRunner() {
    registerHandlers();
}

game::Result EffectRunner::dispatch(const EffectCommand& cmd) {
    auto it = handlers_.find(cmd.id);
    if (it == handlers_.end()) {
        std::cerr << "[chicken-control] don't know an effect called: " << cmd.id << "\n";
        return game::Result::failure("unknown effect");
    }
    game::Result result = it->second(cmd);
    if (result.ok) {
        std::cerr << "[chicken-control] " << cmd.id << "\n";
    } else {
        std::cerr << "[chicken-control] " << cmd.id << " - skipped: " << result.detail << "\n";
    }
    return result;
}

void EffectRunner::tick(double delta_seconds) {
    for (auto& fx : active_timed_effects_) {
        fx.remaining_seconds -= delta_seconds;
    }
    for (auto it = active_timed_effects_.begin(); it != active_timed_effects_.end();) {
        if (it->remaining_seconds <= 0.0) {
            endTimed(it->id);
            it = active_timed_effects_.erase(it);
        } else {
            ++it;
        }
    }
    // Freeze/invert are applied by a hook on the game's own pad-read
    // function now (see game_api.cpp's installHooks()), not polled from
    // here - nothing to do each tick beyond expiring them above.
}

void EffectRunner::endEverything() {
    for (const auto& fx : active_timed_effects_) {
        endTimed(fx.id);
    }
    active_timed_effects_.clear();

    // Clear the control overrides outright rather than relying on the list
    // above. Leaving the player frozen or inverted is the worst thing this
    // mod could do, so it's worth being certain.
    game::setMovementFrozen(false);
    game::setControlsInverted(false);
}

std::vector<std::string> EffectRunner::activeEffects() const {
    std::vector<std::string> names;
    names.reserve(active_timed_effects_.size());
    for (const auto& fx : active_timed_effects_) {
        names.push_back(fx.id);
    }
    return names;
}

void EffectRunner::startTimed(const std::string& id, double duration_seconds) {
    // If it's already running, extend it rather than stacking two of them.
    auto it = std::find_if(active_timed_effects_.begin(), active_timed_effects_.end(),
                            [&](const TimedEffect& fx) { return fx.id == id; });
    if (it != active_timed_effects_.end()) {
        it->remaining_seconds = duration_seconds;
    } else {
        active_timed_effects_.push_back({id, duration_seconds});
    }
}

void EffectRunner::endTimed(const std::string& id) {
    std::cerr << "[chicken-control] " << id << " wore off\n";
    if (id == "player.freeze_movement") {
        game::setMovementFrozen(false);
    } else if (id == "player.invert_controls") {
        game::setControlsInverted(false);
    }
}

void EffectRunner::registerHandlers() {
    // ---- health ----
    handlers_["player.damage_one_heart"] = [](const EffectCommand&) {
        // Half a heart, the same as a regular enemy hit.
        return game::damage(kQuartersPerHeart / 2);
    };

    handlers_["player.heal_full"] = [](const EffectCommand&) {
        return game::healFull();
    };

    handlers_["player.set_hearts"] = [](const EffectCommand& cmd) {
        const int hearts = paramInt(cmd, "hearts", 3);
        return game::setLife(hearts * kQuartersPerHeart);
    };

    // ---- rupees ----
    handlers_["player.give_rupees"] = [](const EffectCommand& cmd) {
        return game::addRupees(paramInt(cmd, "amount", 0));
    };

    handlers_["player.take_rupees"] = [](const EffectCommand& cmd) {
        return game::addRupees(-paramInt(cmd, "amount", 0));
    };

    // ---- magic, arrows, bombs ----
    handlers_["player.set_max_hearts"] = [](const EffectCommand& cmd) {
        return game::setMaxHearts(paramInt(cmd, "hearts", 3));
    };

    handlers_["player.give_magic"] = [](const EffectCommand& cmd) {
        return game::addMagic(paramInt(cmd, "amount", 4));
    };

    handlers_["player.fill_magic"] = [](const EffectCommand&) {
        return game::setMagic(999);  // clamped to the player's actual max internally
    };

    handlers_["player.give_arrows"] = [](const EffectCommand& cmd) {
        return game::addArrows(paramInt(cmd, "amount", 30));
    };

    handlers_["player.give_bombs"] = [](const EffectCommand& cmd) {
        return game::addBombs(paramInt(cmd, "amount", 10));
    };

    // ---- messing with the controls ----
    handlers_["player.freeze_movement"] = [this](const EffectCommand& cmd) {
        if (!game::playerReady()) {
            return game::Result::failure("no save loaded yet");
        }
        game::setMovementFrozen(true);
        startTimed(cmd.id, kFreezeSeconds);
        return game::Result::success();
    };

    handlers_["player.invert_controls"] = [this](const EffectCommand& cmd) {
        if (!game::playerReady()) {
            return game::Result::failure("no save loaded yet");
        }
        game::setControlsInverted(true);
        startTimed(cmd.id, kInvertSeconds);
        return game::Result::success();
    };

    // ---- form ----
    handlers_["player.force_wolf_link"] = [](const EffectCommand&) {
        return game::setWolfForm(true);
    };

    handlers_["player.force_human_link"] = [](const EffectCommand&) {
        return game::setWolfForm(false);
    };

    // ---- items ----
    handlers_["player.set_item_slot"] = [](const EffectCommand& cmd) {
        return game::setItemSlot(paramInt(cmd, "slot", 0), paramInt(cmd, "item_id", 0));
    };

    // ---- world ----
    handlers_["world.spawn_actor"] = [](const EffectCommand& cmd) {
        return game::spawnActor(paramString(cmd, "actor_name", ""));
    };

    handlers_["world.set_time_of_day"] = [](const EffectCommand& cmd) {
        return game::setTimeOfDay(paramInt(cmd, "hour", 12));
    };

    // ---- feedback ----
    handlers_["camera.shake"] = [this](const EffectCommand& cmd) {
        game::Result result = game::shakeScreen(paramInt(cmd, "strength", 4));
        if (result.ok) {
            startTimed(cmd.id, 3.0);
        }
        return result;
    };

    handlers_["hud.message"] = [](const EffectCommand& cmd) {
        // Shows one of the game's own messages by number. Custom text needs
        // a message added through Dusklight's own dialogue support.
        return game::showMessage(paramInt(cmd, "message_id", 0));
    };

    handlers_["audio.play_sfx"] = [](const EffectCommand& cmd) {
        const int sound_id = paramInt(cmd, "sound_id", 0);
        if (sound_id <= 0) {
            return game::Result::failure("bad sound number");
        }
        return game::playSound(static_cast<unsigned int>(sound_id));
    };
}

}  // namespace chickencontrol
