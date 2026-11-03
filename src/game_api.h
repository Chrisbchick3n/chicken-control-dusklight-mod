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

// Warps the player to another stage (area/dungeon), using the game's own
// scene-transition mechanism - the same one a "Continue" load or a Midna
// warp point uses (dComIfGp_setNextStage(), from d/d_com_inf_game.h/.cpp).
// stage_code is one of TP's internal stage names (e.g. "F_SP103" for Ordon
// Village) - see launcher/games/dusklight/locations.py for the list this
// mod is expected to receive. Always targets room 0 at spawn point 0 with
// no specific layer, since only the stage codes themselves - not the
// per-stage room/entrance-point numbers, which differ for every location
// and aren't practical to hand-verify for all of them - could be confirmed
// against the real decompilation. Room 0/point 0 is where most stages'
// primary entrance lands; a few may drop the player somewhere unexpected
// within that stage. Same honest caveat as world.spawn_actor's bosses.
Result warpToStage(const std::string& stage_code);

// -- player form --------------------------------------------------------------
Result setWolfForm(bool wolf);
bool isWolfForm();

// -- items -----------------------------------------------------------------------
Result setItemSlot(int slot, int item_id);

// Gives the player an item by its dItemNo_* id (see
// launcher/games/dusklight/items.py) using execItemGet() - the real
// function-pointer table TP itself uses when the player picks something up
// (include/d/d_item.h / src/d/d_item.cpp), confirmed against the actual
// zeldaret/tp decompilation to be what real "give item" cheats (including
// Crowd Control's own TP pack) use. A handful of item ids listed there are
// confirmed no-ops in that real table (the game never wired up a pickup
// handler for them) - giveItem() still calls execItemGet() for those and
// reports success, since the call itself is real even though nothing
// visibly changes; see the caveat in items.py.
//
// Two real quest items have NO dItemNo_* id at all in the decompilation -
// Fused Shadow fragments and the first Mirror of Twilight shard - they're
// tracked purely by dSv_player_collect_c's own bitfields
// (dComIfGs_onCollectCrystal()/onCollectMirror(), confirmed in
// src/d/d_save.cpp and src/d/d_com_inf_game.h). item_id 165-167 (the other
// three mirror shards, which DO have real ids but whose execItemGet entries
// are confirmed no-ops) and the pseudo-ids 1000-1004 (items.py's
// ID_MIRROR_SHARD_1/ID_FUSED_SHADOW_1..4, which don't correspond to any
// real item id) are special-cased to call those bitfield functions directly
// instead of execItemGet().
Result giveItem(int item_id);

// The reverse of giveItem(), on a best-effort basis. TP's decompilation has
// no unified "take this item away" function - unlike picking something up,
// nothing in the retail game ever needs to un-collect an item, so nothing
// like execItemGet() exists for removal. This handles what's actually
// possible, confirmed against the decompilation for each case:
//   - Fused Shadow fragments / Mirror of Twilight shards: the exact reverse
//     of the special-cased bitfield calls above
//     (dComIfGs_offCollectCrystal()/offCollectMirror()).
//   - The Sword and Master Sword: dComIfGs_offCollectSword(), the same
//     collect-bit clearer item_func_SWORD/MASTER_SWORD's own give-path sets
//     (via setCollectSword()) - confirmed present in
//     include/d/d_com_inf_game.h, though it only clears the "have
//     collected" bit, not necessarily whatever sword is currently equipped.
//   - Key items that live in one of the game's 24 fixed item slots
//     (Boomerang, Spinner, Ball and Chain, Bow, Clawshot, Dominion Rod,
//     Double Clawshot, Lantern, Fishing Rod): clears that item's slot with
//     dComIfGs_setItem(slotNo, dItemNo_NONE_e) - the same pattern the real
//     item_func_W_HOOKSHOT uses to clear the single Clawshot's slot when
//     upgrading to the Double Clawshot. The slot number for each of these
//     was reverse-derived from each item's own item_func_* body in
//     src/d/d_item.cpp (there's no named per-item slot enum in the
//     decompilation - only a generic 24-entry ItemSlots enum), not from a
//     dedicated mapping table, since none exists.
// Anything else (wallet/bomb bag/arrow tiers, armor, shields, and every
// other item.py entry not listed above) fails cleanly with an honest reason
// instead of silently doing nothing or guessing at an unverified mechanism.
Result takeItem(int item_id);

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

// -- timed "chaos" toggles ---------------------------------------------------------
// Added after comparing against every public Crowd Control-style effect set
// for Twilight Princess (the official GameCube pack's published effects and
// Carco's Chaos Mod for Dusklight). Each one is applied by a hook on a real
// daAlink_c function from the decompilation, named next to each entry, so
// it lands at the exact point in Link's frame where the game itself would
// make that decision. While a toggle is off its hook does nothing at all.
enum class Toggle {
    NoSword,       // daAlink_c::checkCutAction - sword swings never start
    NoTransform,   // daAlink_c::procCoMetamorphoseInit - Midna can't transform you
                   //   (never blocks a story cutscene's own transform)
    Invisible,     // daAlink_c::draw - Link isn't drawn
    SuperSpeed,    // daAlink_c::setIceSlipSpeed - x/z velocity boosted
    Slow,          //   same hook - x/z velocity cut
    Slippery,      //   same hook - the ground is treated as ice (dBgW_SPCODE_ICE),
                   //   so the game's own ice-sliding code runs everywhere
    MoonJump,      //   same hook - hold R+A to rise (Dusklight's own cheat mechanic)
    LowGravity,    //   same hook - most of each frame's gravity is undone
    OneHitKO,      // daAlink_c::damageMagnification - returns 9999, the same value
                   //   Dusklight's own "Instant Death" setting uses
    Invincible,    //   same hook - returns 0 (no damage taken)
    DoubleDamage,  //   same hook - the game's own multiplier, doubled
    SwapButtons,   // mDoCPd_c::convert - A<->B and X<->Y swapped
    SwordDrain,    // daAlink_c::setCutType - every sword swing costs a quarter heart
    Count
};
void setToggle(Toggle which, bool on);
bool isToggleOn(Toggle which);

// Stretches/shrinks Link's model (visual only - his collision is unchanged,
// so he still fits through every doorway). Applied every frame after
// daAlink_c::setMatrix() via J3DModel::setBaseScale(). 1,1,1 = normal.
void setLinkScale(float x, float y, float z);

// Temporarily empties the item slot(s) an item lives in, remembering what
// was there so restoreDisabledItems() can put it back - the same approach
// Crowd Control's own TP pack takes for "Disable Clawshot". Only items whose
// slot is confirmed (see takeItem()) are supported; Clawshot also clears the
// Double Clawshot slot. Use restoreDisabledItems() to undo.
Result disableItem(int item_id);
void restoreDisabledItems();

// Throws Link straight up for about a second (speed.y held at 56, the same
// value Dusklight's built-in Moon Jump uses).
Result rocketLaunch();

// A real bomb explosion on Link: the nine particle effects and the sound
// daNbomb_c uses when a normal bomb goes off (d_a_nbomb.cpp), plus a screen
// quake, plus `quarter_hearts` of damage.
Result explodeOnLink(int quarter_hearts);

// -- setup -------------------------------------------------------------------------
// Installs the game-function hooks this file depends on (currently just the
// pad-read hook used by the input overrides above). Call once from
// mod_initialize, after services are resolved. Safe to call when built
// without the game (does nothing).
void installHooks();

}  // namespace game
}  // namespace chickencontrol
