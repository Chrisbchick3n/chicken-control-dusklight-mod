#include "effects.h"

#include <algorithm>
#include <iostream>

namespace chickencontrol {

namespace {
// One full heart is four of the game's health units.
constexpr int kQuartersPerHeart = 4;

// How long the timed effects last, in seconds. These match the defaults
// shown in launcher/effects.py.
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
    for (int i = 0; i < static_cast<int>(game::Toggle::Count); ++i) {
        game::setToggle(static_cast<game::Toggle>(i), false);
    }
    game::setLinkScale(1.0f, 1.0f, 1.0f);
    game::restoreDisabledItems();
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

namespace {
// Timed chaos effects: effect id -> the toggle it turns on, and how long it
// lasts if the app doesn't send a "seconds" param.
struct ToggleEffect {
    const char* id;
    game::Toggle toggle;
    double default_seconds;
};
constexpr ToggleEffect kToggleEffects[] = {
    {"player.no_sword", game::Toggle::NoSword, 20.0},
    {"player.no_transform", game::Toggle::NoTransform, 30.0},
    {"player.invisible", game::Toggle::Invisible, 20.0},
    {"player.super_speed", game::Toggle::SuperSpeed, 20.0},
    {"player.slow", game::Toggle::Slow, 20.0},
    {"player.slippery", game::Toggle::Slippery, 30.0},
    {"player.moon_jump", game::Toggle::MoonJump, 30.0},
    {"player.low_gravity", game::Toggle::LowGravity, 20.0},
    {"player.one_hit_ko", game::Toggle::OneHitKO, 30.0},
    {"player.invincible", game::Toggle::Invincible, 20.0},
    {"player.double_damage", game::Toggle::DoubleDamage, 30.0},
    {"player.swap_buttons", game::Toggle::SwapButtons, 20.0},
    {"player.sword_drain", game::Toggle::SwordDrain, 30.0},
};

// Size effects: id -> x, y, z scale.
struct SizeEffect {
    const char* id;
    float x, y, z;
};
constexpr SizeEffect kSizeEffects[] = {
    {"player.giant", 2.0f, 2.0f, 2.0f},
    {"player.tiny", 0.4f, 0.4f, 0.4f},
    {"player.wide", 2.2f, 1.0f, 2.2f},
    {"player.paper_thin", 0.15f, 1.0f, 1.0f},
    {"player.tall", 1.0f, 2.2f, 1.0f},
};
constexpr double kSizeSeconds = 30.0;
constexpr double kDisableItemSeconds = 30.0;

// Opposites cancel each other out instead of fighting.
void clearOpposite(game::Toggle t) {
    using game::Toggle;
    switch (t) {
        case Toggle::SuperSpeed: game::setToggle(Toggle::Slow, false); break;
        case Toggle::Slow: game::setToggle(Toggle::SuperSpeed, false); break;
        case Toggle::OneHitKO: game::setToggle(Toggle::Invincible, false); break;
        case Toggle::Invincible:
            game::setToggle(Toggle::OneHitKO, false);
            game::setToggle(Toggle::DoubleDamage, false);
            break;
        default: break;
    }
}
}  // namespace

void EffectRunner::endTimed(const std::string& id) {
    std::cerr << "[chicken-control] " << id << " wore off\n";
    if (id == "player.freeze_movement") {
        game::setMovementFrozen(false);
    } else if (id == "player.invert_controls") {
        game::setControlsInverted(false);
    } else if (id == "player.size") {
        game::setLinkScale(1.0f, 1.0f, 1.0f);
    } else if (id == "player.disable_item") {
        game::restoreDisabledItems();
    } else {
        for (const auto& fx : kToggleEffects) {
            if (id == fx.id) game::setToggle(fx.toggle, false);
        }
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

    handlers_["world.give_item"] = [](const EffectCommand& cmd) {
        return game::giveItem(paramInt(cmd, "item_id", 0));
    };

    handlers_["world.take_item"] = [](const EffectCommand& cmd) {
        return game::takeItem(paramInt(cmd, "item_id", 0));
    };

    // ---- world ----
    handlers_["world.spawn_actor"] = [](const EffectCommand& cmd) {
        return game::spawnActor(paramString(cmd, "actor_name", ""));
    };

    handlers_["world.teleport"] = [](const EffectCommand& cmd) {
        return game::warpToStage(paramString(cmd, "stage_code", ""));
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

    // ---- chaos (timed toggles) ----
    // Each accepts an optional "seconds" param; otherwise uses its default.
    for (const auto& fx : kToggleEffects) {
        const game::Toggle toggle = fx.toggle;
        const double default_seconds = fx.default_seconds;
        handlers_[fx.id] = [this, toggle, default_seconds](const EffectCommand& cmd) {
            if (!game::playerReady()) return game::Result::failure("no save loaded yet");
            clearOpposite(toggle);
            game::setToggle(toggle, true);
            startTimed(cmd.id, paramInt(cmd, "seconds", static_cast<int>(default_seconds)));
            return game::Result::success();
        };
    }

    // ---- size (one size at a time; a new one replaces the old) ----
    for (const auto& fx : kSizeEffects) {
        const float x = fx.x, y = fx.y, z = fx.z;
        handlers_[fx.id] = [this, x, y, z](const EffectCommand& cmd) {
            if (!game::playerReady()) return game::Result::failure("no save loaded yet");
            game::setLinkScale(x, y, z);
            startTimed("player.size", paramInt(cmd, "seconds", static_cast<int>(kSizeSeconds)));
            return game::Result::success();
        };
    }

    // ---- take an item away for a while (e.g. Disable Clawshot) ----
    handlers_["player.disable_item"] = [this](const EffectCommand& cmd) {
        game::Result r = game::disableItem(paramInt(cmd, "item_id", 68));
        if (r.ok) {
            startTimed(cmd.id, paramInt(cmd, "seconds", static_cast<int>(kDisableItemSeconds)));
        }
        return r;
    };

    // ---- one-shots ----
    handlers_["player.rocket_launch"] = [](const EffectCommand&) {
        return game::rocketLaunch();
    };

    handlers_["player.explode"] = [](const EffectCommand& cmd) {
        // Default: two hearts, like getting caught in your own bomb.
        return game::explodeOnLink(paramInt(cmd, "hearts", 2) * kQuartersPerHeart);
    };

    // Clears every running effect - the "fix it" button if anything gets
    // stuck (same idea as Crowd Control's Admin effects).
    handlers_["admin.stop_all"] = [this](const EffectCommand&) {
        endEverything();
        return game::Result::success();
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
