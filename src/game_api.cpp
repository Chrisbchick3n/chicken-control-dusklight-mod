#include "game_api.h"

#include <algorithm>

#ifdef CHICKEN_CONTROL_HAS_GAME
// These come from the Twilight Princess decompilation that Dusklight is
// built on. Paths follow that project's include layout.
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_save.h"
#include "d/d_vibration.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_msg_mng.h"
#include "m_Do/m_Do_controller_pad.h"
#include "SSystem/SComponent/c_API_controller_pad.h"
#include "mods/svc/hook.hpp"

IMPORT_SERVICE(HookService, svc_hook);

// mDoCPd_c::convert() is what copies the real hardware stick into the
// struct gameplay code reads (m_Do_controller_pad.cpp) - it's called once
// per pad, every frame, from the same place the game reads its own input.
// Hooking it (rather than polling from mod_update, which raced against
// this and mostly lost) means our override always lands after the real
// value and before anything downstream reads it.
DEFINE_HOOK(&mDoCPd_c::convert, ConvertPad);
#endif

namespace chickencontrol {
namespace game {

namespace {
// The game's clock is stored as hours x 15, so a whole day is 360.
constexpr float kTimeUnitsPerHour = 15.0f;

// The save flag that gets set once the player can turn into a wolf at will.
// Before that, forcing the change does nothing useful.
constexpr unsigned short kEventBitTransformUnlocked = 0x0D04;

// Live state for the timed effects.
bool g_movement_frozen = false;
bool g_controls_inverted = false;
}  // namespace

#ifdef CHICKEN_CONTROL_HAS_GAME

// ---------------------------------------------------------------------------
// Real implementation - compiled when building against Dusklight.
// ---------------------------------------------------------------------------

namespace {
fopAc_ac_c* playerActor() {
    return dComIfGp_getPlayer(0);
}
}  // namespace

bool playerReady() {
    return playerActor() != nullptr;
}

int getLife() {
    return static_cast<int>(dComIfGs_getLife());
}

int getMaxLife() {
    return static_cast<int>(dComIfGs_getMaxLife());
}

Result setLife(int quarter_hearts) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    const int max_life = getMaxLife();
    const int clamped = std::clamp(quarter_hearts, 0, max_life);
    dComIfGs_setLife(static_cast<u16>(clamped));
    return Result::success();
}

Result damage(int quarter_hearts) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (quarter_hearts <= 0) return Result::failure("damage must be more than zero");
    return setLife(getLife() - quarter_hearts);
}

Result healFull() {
    if (!playerReady()) return Result::failure("no save loaded yet");
    return setLife(getMaxLife());
}

int getRupees() {
    return static_cast<int>(dComIfGs_getRupee());
}

int getRupeeCapacity() {
    return static_cast<int>(dComIfGs_getRupeeMax());
}

Result setRupees(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    // Never go past the wallet the player actually has, or below zero.
    const int capacity = getRupeeCapacity();
    const int upper = capacity > 0 ? capacity : 0;
    dComIfGs_setRupee(static_cast<u16>(std::clamp(amount, 0, upper)));
    return Result::success();
}

Result addRupees(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    return setRupees(getRupees() + amount);
}

Result setMaxHearts(int hearts) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (hearts < 1) return Result::failure("must be at least one heart");
    // Same units as getLife()/setLife() - quarter-hearts.
    dComIfGs_setMaxLife(static_cast<u8>(std::clamp(hearts * 4, 4, 255)));
    return Result::success();
}

Result setMagic(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    const int max_magic = dComIfGs_getMaxMagic();
    dComIfGs_setMagic(static_cast<u8>(std::clamp(amount, 0, max_magic > 0 ? max_magic : 255)));
    return Result::success();
}

Result addMagic(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    return setMagic(static_cast<int>(dComIfGs_getMagic()) + amount);
}

int getArrows() { return dComIfGs_getArrowNum(); }

Result setArrows(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    const int max_arrows = dComIfGs_getArrowMax();
    dComIfGs_setArrowNum(static_cast<u8>(std::clamp(amount, 0, max_arrows > 0 ? max_arrows : 255)));
    return Result::success();
}

Result addArrows(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    return setArrows(getArrows() + amount);
}

int getBombs() { return dComIfGs_getBombNum(0); }

Result setBombs(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    // Bag 0 is the first (always-available) bomb bag; matches the simple
    // single-number "how many bombs" idea this effect is going for.
    const int max_bombs = dComIfGs_getBombMax();
    dComIfGs_setBombNum(0, static_cast<u8>(std::clamp(amount, 0, max_bombs > 0 ? max_bombs : 255)));
    return Result::success();
}

Result addBombs(int amount) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    return setBombs(getBombs() + amount);
}

Result setTimeOfDay(int hour) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (hour < 0 || hour > 23) return Result::failure("hour must be between 0 and 23");
    dComIfGs_setTime(static_cast<f32>(hour) * kTimeUnitsPerHour);
    return Result::success();
}

Result spawnActor(const std::string& actor_name) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (actor_name.empty()) return Result::failure("no actor name given");

    fopAc_ac_c* player = playerActor();
    cXyz spawn_pos = player->current.pos;
    // Put it a little away from the player rather than inside them.
    spawn_pos.z += 150.0f;

    const int room_no = fopAcM_GetRoomNo(player);
    fopAc_ac_c* created = fopAcM_fastCreate(actor_name.c_str(), 0, &spawn_pos, room_no,
                                             nullptr, nullptr, nullptr, nullptr);
    if (created == nullptr) {
        return Result::failure("the game didn't recognise that actor name");
    }
    return Result::success();
}

Result warpToStage(const std::string& stage_code) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (stage_code.empty()) return Result::failure("no stage code given");
    // dComIfGp_setNextStage() is the real function TP's own scene manager
    // uses for field/dungeon transitions (loading a save, a Midna warp
    // point, and so on) - it queues the transition (stage name, spawn
    // point, room, layer) on g_dComIfG_gameInfo.play for the engine to
    // pick up and load on its own, rather than swapping the world out
    // directly. Point 0, room 0, layer -1 (TP's own "no specific layer"
    // value - see this same function's clamp for >=15) is the closest
    // thing to a generic "walk in the default way" for a stage whose real
    // entrance-point number wasn't confirmed here - see the caveat on
    // warpToStage() in game_api.h.
    dComIfGp_setNextStage(stage_code.c_str(), /*point*/ 0, /*roomNo*/ 0, /*layer*/ -1);
    return Result::success();
}

Result setWolfForm(bool wolf) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (!dComIfGs_isEventBit(kEventBitTransformUnlocked)) {
        return Result::failure("the player can't transform yet at this point in the story");
    }
    if (isWolfForm() == wolf) return Result::success();  // already there

    // dComIfGs_setTransformStatus() only flips the save flag - it's what
    // daAlink_c::changeWolf()/changeLink() call internally once they've
    // finished actually swapping the live model/animation/state over, but
    // setting it directly (as this used to do) leaves the player looking
    // and playing as whatever form they already were. Call the real
    // transform functions instead; they set the flag themselves as part of
    // doing the swap for real. Link is always a daAlink_c under the actor
    // pointer dComIfGp_getPlayer(0) hands back, in both forms.
    daAlink_c* link = static_cast<daAlink_c*>(playerActor());
    if (wolf) {
        link->changeWolf();
    } else {
        link->changeLink(0);
    }
    return Result::success();
}

bool isWolfForm() {
    return dComIfGs_getTransformStatus() != 0;
}

Result setItemSlot(int slot, int item_id) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (slot < 0) return Result::failure("bad item slot");
    dComIfGs_setSelectItemIndex(slot, static_cast<u8>(item_id));
    return Result::success();
}

namespace {
// items.py's ID_MIRROR_SHARD_1/ID_FUSED_SHADOW_1..4 - see the comment on
// giveItem()/takeItem() in game_api.h for why these exist at all.
constexpr int kIdMirrorShard1 = 1000;
constexpr int kIdFusedShadow1 = 1001;
constexpr int kIdFusedShadow4 = 1004;
}  // namespace

Result giveItem(int item_id) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (item_id == 255) return Result::failure("\"None\" isn't a real item");

    if (item_id >= kIdFusedShadow1 && item_id <= kIdFusedShadow4) {
        dComIfGs_onCollectCrystal(static_cast<u8>(item_id - kIdFusedShadow1));
        return Result::success();
    }
    if (item_id == kIdMirrorShard1) {
        dComIfGs_onCollectMirror(0);
        return Result::success();
    }
    if (item_id == 165 || item_id == 166 || item_id == 167) {
        // execItemGet() no-ops for these three (confirmed in the decomp's
        // own item_func_MIRROR_PIECE_2/3/4 bodies) - onCollectMirror() is
        // the real grant path. Shard 1 is index 0 (see kIdMirrorShard1
        // above); shards 2/3/4 (these ids) are indices 1/2/3.
        dComIfGs_onCollectMirror(static_cast<u8>(item_id - 165 + 1));
        return Result::success();
    }
    if (item_id < 0 || item_id > 255) return Result::failure("bad item id");

    // execItemGet() is TP's own real "the player just got this item" table
    // - the same one a field/dungeon pickup calls. A few ids are confirmed
    // no-ops in that real table (see items.py's DUSKLIGHT_STORY_ITEMS
    // comment) - still called and reported as success, since the call
    // itself is exactly what the real game would do for that id.
    execItemGet(static_cast<u8>(item_id));
    return Result::success();
}

Result takeItem(int item_id) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (item_id == 255) return Result::failure("\"None\" isn't a real item");

    if (item_id >= kIdFusedShadow1 && item_id <= kIdFusedShadow4) {
        dComIfGs_offCollectCrystal(static_cast<u8>(item_id - kIdFusedShadow1));
        return Result::success();
    }
    if (item_id == kIdMirrorShard1) {
        dComIfGs_offCollectMirror(0);
        return Result::success();
    }
    if (item_id == 165 || item_id == 166 || item_id == 167) {
        dComIfGs_offCollectMirror(static_cast<u8>(item_id - 165 + 1));
        return Result::success();
    }

    // Key items that live in one of the game's fixed item slots - clearing
    // the slot is the real reverse of the matching item_func_* in
    // src/d/d_item.cpp (see the comment on takeItem() in game_api.h for how
    // these slot numbers were found).
    switch (item_id) {
        case 64: dComIfGs_setItem(SLOT_0, dItemNo_NONE_e); return Result::success();   // Boomerang
        case 72: dComIfGs_setItem(SLOT_1, dItemNo_NONE_e); return Result::success();   // Lantern
        case 65: dComIfGs_setItem(SLOT_2, dItemNo_NONE_e); return Result::success();   // Spinner
        case 67: dComIfGs_setItem(SLOT_4, dItemNo_NONE_e); return Result::success();   // Bow
        case 66: dComIfGs_setItem(SLOT_6, dItemNo_NONE_e); return Result::success();   // Ball and Chain
        case 70: dComIfGs_setItem(SLOT_8, dItemNo_NONE_e); return Result::success();   // Dominion Rod
        case 68: dComIfGs_setItem(SLOT_9, dItemNo_NONE_e); return Result::success();   // Clawshot
        case 71: dComIfGs_setItem(SLOT_10, dItemNo_NONE_e); return Result::success();  // Double Clawshot
        case 74: dComIfGs_setItem(SLOT_20, dItemNo_NONE_e); return Result::success();  // Fishing Rod
        case 40: dComIfGs_offCollectSword(COLLECT_ORDON_SWORD); return Result::success();
        case 41: dComIfGs_offCollectSword(COLLECT_MASTER_SWORD); return Result::success();
        default: break;
    }
    return Result::failure("taking this item back isn't supported - no confirmed way to undo it");
}

Result shakeScreen(int strength) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    // The game already has eight shake strengths, so map 1-8 onto those.
    const int level = std::clamp(strength, 1, 8);
    const int vib_mode = VIBMODE_Q_POWER1 + (level - 1);
    cXyz at = playerActor()->current.pos;
    dComIfGp_getVibration().StartQuake(vib_mode, 0x1F, at);
    return Result::success();
}

Result showMessage(int message_id) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (message_id <= 0) return Result::failure("bad message number");
    fopMsgM_messageSet(static_cast<u32>(message_id), 0);
    return Result::success();
}

Result playSound(unsigned int sound_id) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    fopAcM_seStartCurrent(playerActor(), sound_id, 0);
    return Result::success();
}

namespace {
// Runs after the game has copied real hardware input into a pad's
// interface struct. Only pad 0 (the player) is touched; convert() is
// called once per pad (up to 4) every frame, and there's no argument
// telling us which one this call is for, so we compare against the one
// gameplay code actually reads from.
void onConvertPadPost(ModContext*, void* args, void*, void*) {
    if (!g_movement_frozen && !g_controls_inverted) return;

    interface_of_controller_pad& pad = mods::arg_ref<interface_of_controller_pad>(args, 0);
    if (&pad != &mDoCPd_c::getCpadInfo(0)) return;

    if (g_movement_frozen) {
        pad.mMainStickPosX = 0.0f;
        pad.mMainStickPosY = 0.0f;
        pad.mMainStickValue = 0.0f;
    } else if (g_controls_inverted) {
        pad.mMainStickPosX = -pad.mMainStickPosX;
        pad.mMainStickPosY = -pad.mMainStickPosY;
        // The player reads the stick's direction as well as its position,
        // so that has to be turned around too or movement fights itself.
        pad.mMainStickAngle = static_cast<s16>(pad.mMainStickAngle + 0x8000);
    }
}
}  // namespace

void installHooks() {
    mods::hook::add_post<ConvertPad>(onConvertPadPost);
}

#else

// ---------------------------------------------------------------------------
// Stand-in - compiled when building without the game, so the networking and
// routing code can still be built and tested on its own.
// ---------------------------------------------------------------------------

namespace {
constexpr const char* kNoGame = "built without the game, so this does nothing";
}

bool playerReady() { return false; }
int getLife() { return 0; }
int getMaxLife() { return 0; }
Result setLife(int) { return Result::failure(kNoGame); }
Result damage(int) { return Result::failure(kNoGame); }
Result healFull() { return Result::failure(kNoGame); }
int getRupees() { return 0; }
int getRupeeCapacity() { return 0; }
Result setRupees(int) { return Result::failure(kNoGame); }
Result addRupees(int) { return Result::failure(kNoGame); }
Result setMaxHearts(int) { return Result::failure(kNoGame); }
Result setMagic(int) { return Result::failure(kNoGame); }
Result addMagic(int) { return Result::failure(kNoGame); }
int getArrows() { return 0; }
Result setArrows(int) { return Result::failure(kNoGame); }
Result addArrows(int) { return Result::failure(kNoGame); }
int getBombs() { return 0; }
Result setBombs(int) { return Result::failure(kNoGame); }
Result addBombs(int) { return Result::failure(kNoGame); }
Result setTimeOfDay(int) { return Result::failure(kNoGame); }
Result spawnActor(const std::string&) { return Result::failure(kNoGame); }
Result warpToStage(const std::string&) { return Result::failure(kNoGame); }
Result setWolfForm(bool) { return Result::failure(kNoGame); }
bool isWolfForm() { return false; }
Result setItemSlot(int, int) { return Result::failure(kNoGame); }
Result giveItem(int) { return Result::failure(kNoGame); }
Result takeItem(int) { return Result::failure(kNoGame); }
Result shakeScreen(int) { return Result::failure(kNoGame); }
Result showMessage(int) { return Result::failure(kNoGame); }
Result playSound(unsigned int) { return Result::failure(kNoGame); }
void installHooks() {}

#endif  // CHICKEN_CONTROL_HAS_GAME

// ---------------------------------------------------------------------------
// Shared between both builds.
// ---------------------------------------------------------------------------

void setMovementFrozen(bool frozen) { g_movement_frozen = frozen; }
void setControlsInverted(bool inverted) { g_controls_inverted = inverted; }
bool isMovementFrozen() { return g_movement_frozen; }
bool areControlsInverted() { return g_controls_inverted; }

}  // namespace game
}  // namespace chickencontrol
