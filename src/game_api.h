// game_api.h - the bridge between Chicken Control's effects and Twilight
// Princess's own code.
//
// Everything here is a thin wrapper around a real function or field from
// the game, using the names from the Twilight Princess decompilation that
// Dusklight is built on (https://github.com/zeldaret/tp).
//
// TWO BUILD MODES
//   Built as part of a Dusklight mod (CHICKEN_CONTROL_HAS_GAME defined by
//   CMake when DUSKLIGHT_DIR is set), these call straight into the game.
//   Built on its own, they compile to harmless no-ops that report "not
//   available", so the networking and routing code can still be built and
//   tested without a copy of the game.

#pragma once

#include <string>

namespace chickencontrol {
namespace game {

// Every call reports whether it actually did something, so effects can be
// honest about what happened instead of silently doing nothing.
struct Result {
    bool ok = false;
    std::string detail;

    static Result success() { return {true, ""}; }
    static Result failure(const char* why) { return {false, why}; }
};

// Is a save loaded and the player in the world? Everything else checks
// this first - none of it is safe on the title screen.
bool playerReady();

// -- health (measured in quarter-hearts, as the game stores it) ------------
int getLife();
int getMaxLife();
Result setLife(int quarter_hearts);
Result damage(int quarter_hearts);
Result healFull();

// -- rupees ----------------------------------------------------------------
int getRupees();
int getRupeeCapacity();
Result setRupees(int amount);
Result addRupees(int amount);

// -- world ------------------------------------------------------------------
// Hour runs 0-23 and is converted to the game's own clock scale.
Result setTimeOfDay(int hour);

// Spawns an actor by its name, next to the player.
Result spawnActor(const std::string& actor_name);

// -- player form --------------------------------------------------------------
Result setWolfForm(bool wolf);
bool isWolfForm();

// -- items -----------------------------------------------------------------------
Result setItemSlot(int slot, int item_id);

// -- feedback ------------------------------------------------------------------
// Shakes the screen and rumbles the controller. Strength is 1-8.
Result shakeScreen(int strength);

// Shows one of the game's own messages by its number.
Result showMessage(int message_id);

// Plays one of the game's sound effects by its id.
Result playSound(unsigned int sound_id);

// -- input -------------------------------------------------------------------------
// Used by the timed effects. The mod checks these every frame and adjusts
// the stick before the player reads it.
void setMovementFrozen(bool frozen);
void setControlsInverted(bool inverted);
bool isMovementFrozen();
bool areControlsInverted();

// Called once per frame from mod_update so the input tweaks above can be
// applied while they're active.
void applyInputOverrides();

}  // namespace game
}  // namespace chickencontrol
