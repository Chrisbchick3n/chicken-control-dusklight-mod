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

// -- magic, arrows, bombs (all clamp to the player's current capacity) -----
Result setMaxHearts(int hearts);          // in whole heart containers
Result setMagic(int amount);
Result addMagic(int amount);
int getArrows();
Result setArrows(int amount);
Result addArrows(int amount);
int getBombs();
Result setBombs(int amount);
Result addBombs(int amount);

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
// Used by the timed effects. Applied via a hook on the game's own pad-read
// function (see installHooks()) rather than polled from mod_update, so the
// override always lands after the game's own hardware read and can't be
// overwritten by it later in the same frame.
void setMovementFrozen(bool frozen);
void setControlsInverted(bool inverted);
bool isMovementFrozen();
bool areControlsInverted();

// -- setup -------------------------------------------------------------------------
// Installs the game-function hooks this file depends on (currently just the
// pad-read hook used by the input overrides above). Call once from
// mod_initialize, after services are resolved. Safe to call when built
// without the game (does nothing).
void installHooks();

}  // namespace game
}  // namespace chickencontrol
