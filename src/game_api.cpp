#include "game_api.h"

#include <algorithm>

#ifdef CHICKEN_CONTROL_HAS_GAME
// These come from the Twilight Princess decompilation that Dusklight is
// built on. Paths follow that project's include layout.
#include "d/d_com_inf_game.h"
#include "d/d_save.h"
#include "d/d_vibration.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_msg_mng.h"
#include "m_Do/m_Do_controller_pad.h"
#include "SSystem/SComponent/c_API_controller_pad.h"
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

Result setWolfForm(bool wolf) {
    if (!playerReady()) return Result::failure("no save loaded yet");
    if (!dComIfGs_isEventBit(kEventBitTransformUnlocked)) {
        return Result::failure("the player can't transform yet at this point in the story");
    }
    // Non-zero means wolf.
    dComIfGs_setTransformStatus(wolf ? 1 : 0);
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

void applyInputOverrides() {
    if (!playerReady()) return;
    if (!g_movement_frozen && !g_controls_inverted) return;

    // The stick is read fresh every frame, so changing it here lands before
    // the player actor gets to use it.
    interface_of_controller_pad& pad = mDoCPd_c::getCpadInfo(0);

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
Result setTimeOfDay(int) { return Result::failure(kNoGame); }
Result spawnActor(const std::string&) { return Result::failure(kNoGame); }
Result setWolfForm(bool) { return Result::failure(kNoGame); }
bool isWolfForm() { return false; }
Result setItemSlot(int, int) { return Result::failure(kNoGame); }
Result shakeScreen(int) { return Result::failure(kNoGame); }
Result showMessage(int) { return Result::failure(kNoGame); }
Result playSound(unsigned int) { return Result::failure(kNoGame); }
void applyInputOverrides() {}

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
