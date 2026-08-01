#include "DevBenchSneakMatrix.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "DevBenchAPI.h"
#include "DevBenchInput.h"
#include "Hooks.h"
#include "Settings.h"
#include "logger.h"

namespace DevBenchSneakMatrix {
enum class NativePumpPhase {
    kForward,
    kSprint,
    kPostSprint,
    kRelease,
    kDone,
    kFailed
};

struct NativeInputSession {
    std::mutex mutex;
    std::atomic_bool cancelled{ false };
    bool release_requested = false;
    bool release_acknowledged = false;
    bool in_frame = false;
    bool walking = false;
    bool weapons_drawn = false;
    std::uint32_t expected_right_weapon = 0;
    std::uint32_t expected_left_weapon = 0;
    RE::ButtonEvent* forward_event = nullptr;
    RE::ButtonEvent* sprint_event = nullptr;
    std::uint32_t forward_key = RE::ControlMap::kInvalid;
    std::uint32_t sprint_key = RE::ControlMap::kInvalid;
    NativePumpPhase phase = NativePumpPhase::kForward;
    std::chrono::steady_clock::time_point forward_started{};
    std::chrono::steady_clock::time_point sprint_started{};
    RE::NiPoint3 movement_origin{};
    RE::NiPoint3 sprint_origin{};
    float forward_held_seconds = 0.0F;
    float sprint_held_seconds = 0.0F;
    bool forward_owned = false;
    bool sprint_owned = false;
    bool forward_down_dispatched = false;
    std::uint32_t forward_held_ticks = 0;
    bool move_input_observed = false;
    bool movement_observed = false;
    bool actor_walking_setup = false;
    bool sprint_down_dispatched = false;
    std::uint32_t sprint_held_ticks = 0;
    bool sprint_held_dispatched = false;
    bool sprint_sequence_dispatched = false;
    bool sprint_sequence_complete = false;
    std::uint32_t post_sprint_forward_ticks = 0;
    bool outcome_observed = false;
    bool sneaking_after = true;
    bool running_after = false;
    bool actor_walking_after = true;
    bool moving_after = false;
    bool continued_displacement = false;
    bool weapon_matches_after = false;
    bool equipped_weapons_match_after = false;
    bool sprint_up_dispatched = false;
    bool forward_up_dispatched = false;
    std::string error;
};
}
namespace {
using Clock = std::chrono::steady_clock;

constexpr auto kMainThreadTimeout = std::chrono::seconds(3);
constexpr auto kStateTimeout = std::chrono::seconds(3);
constexpr auto kMovementTimeout = std::chrono::milliseconds(2500);
constexpr auto kOutcomeTimeout = std::chrono::milliseconds(1500);
constexpr auto kTickInterval = std::chrono::milliseconds(40);
constexpr auto kSneakEventInterval = std::chrono::milliseconds(50);
constexpr auto kStationarySampleInterval = std::chrono::milliseconds(80);
constexpr auto kInitialReleaseTimeout = std::chrono::milliseconds(500);
constexpr float kMinimumHorizontalMovementSquared = 1.0F;
constexpr float kMaximumStationaryDriftSquared = 0.01F;
constexpr float kMoveInputEpsilonSquared = 0.0001F;
constexpr float kRequiredSprintHeldSeconds = 0.50F;
constexpr float kMinimumReleaseDuration = 0.05F;

struct CaseConfig {
    const char* tool_name;
    bool walking;
    bool weapons_drawn;
};

constexpr CaseConfig kCases[] = {
    { "sprint_cancels_sneak.matrix.sneak_run_sheathed", false, false },
    { "sprint_cancels_sneak.matrix.sneak_walk_sheathed", true, false },
    { "sprint_cancels_sneak.matrix.sneak_run_drawn", false, true },
    { "sprint_cancels_sneak.matrix.sneak_walk_drawn", true, true },
};

struct EquippedWeaponFormIDs {
    std::uint32_t right = 0;
    std::uint32_t left = 0;

    [[nodiscard]] bool HasWeapon() const { return right != 0 || left != 0; }
    bool operator==(const EquippedWeaponFormIDs&) const = default;
};

struct CaseResult {
    bool ok = false;
    bool player_loaded = false;
    bool riverwood_exterior = false;
    bool menus_clear = false;
    bool dispatcher_captured = false;
    bool tudm_absent = false;
    bool baseline_captured = false;
    bool eligible = false;
    bool equipped_weapon_present = false;
    bool forward_mapping_valid = false;
    bool sprint_mapping_valid = false;
    bool sneak_mapping_valid = false;
    bool movement_handler_ready = false;
    bool setup_weapon = false;
    bool setup_walking = false;
    bool setup_sneaking = false;
    bool stationary_baseline = false;
    bool forward_event_created = false;
    bool sprint_event_created = false;
    bool forward_down_dispatched = false;
    std::uint32_t forward_held_ticks = 0;
    bool move_input_observed = false;
    bool movement_observed = false;
    bool actor_walking_setup = false;
    bool sprint_down_dispatched = false;
    std::uint32_t sprint_held_ticks = 0;
    bool sprint_held_dispatched = false;
    bool sprint_sequence_dispatched = false;
    bool sprint_sequence_complete = false;
    std::uint32_t post_sprint_forward_ticks = 0;
    bool outcome_observed = false;
    bool sneaking_after = true;
    bool running_after = false;
    bool actor_walking_after = true;
    bool moving_after = false;
    bool continued_displacement = false;
    bool weapon_matches_after = false;
    bool equipped_weapons_match_after = false;
    bool cleanup_attempted = false;
    bool sprint_up_dispatched = false;
    bool forward_up_dispatched = false;
    bool forward_up_fallback = false;
    bool forward_up_direct_retry = false;
    bool move_input_neutral = false;
    bool movement_stopped = false;
    bool auto_move_after_cleanup = false;
    bool stable_position_after_cleanup = false;
    bool cleanup_sneaking_restored = false;
    bool cleanup_running_restored = false;
    bool cleanup_weapon_restored = false;
    bool cleanup_equipment_restored = false;
    bool cleanup_settled = false;
    std::string error;
};

struct CaseSession {
    CaseConfig config{};
    CaseResult result{};
    RE::NiPoint3 movement_origin{};
    RE::NiPoint3 sprint_origin{};
    bool original_sneaking = false;
    bool original_running = true;
    bool original_weapon_drawn = false;
    EquippedWeaponFormIDs equipped_weapon_form_ids{};
    RE::ButtonEvent* forward_event = nullptr;
    RE::ButtonEvent* sprint_event = nullptr;
    RE::ButtonEvent* sneak_event = nullptr;
    RE::ButtonEvent* ready_weapon_event = nullptr;
    std::uint32_t forward_key = RE::ControlMap::kInvalid;
    std::uint32_t sprint_key = RE::ControlMap::kInvalid;
    std::uint32_t sneak_key = RE::ControlMap::kInvalid;
    std::uint32_t ready_weapon_key = RE::ControlMap::kInvalid;
    bool forward_owned = false;
    bool sprint_owned = false;
    bool sneak_owned = false;
    float forward_held_seconds = 0.0F;
    float sprint_held_seconds = 0.0F;
    std::shared_ptr<std::atomic_bool> cancelled =
        std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<DevBenchSneakMatrix::NativeInputSession> native_input;
};

struct PositionSample {
    bool valid = false;
    RE::NiPoint3 position{};
};

std::atomic_bool g_busy{ false };
std::mutex g_native_session_mutex;
std::shared_ptr<DevBenchSneakMatrix::NativeInputSession> g_native_session;

bool Cancelled(const CaseSession& session) {
    return session.cancelled && session.cancelled->load();
}

template <class Fn>
auto OnMainThread(Fn&& fn) -> std::optional<std::invoke_result_t<Fn>> {
    using Result = std::invoke_result_t<Fn>;
    const auto completion = std::make_shared<std::promise<Result>>();
    auto future = completion->get_future();
    const auto tasks = SKSE::GetTaskInterface();
    if (!tasks) return std::nullopt;
    tasks->AddTask([completion, fn = std::forward<Fn>(fn)]() mutable {
        try {
            completion->set_value(fn());
        } catch (...) {
            completion->set_exception(std::current_exception());
        }
    });
    if (future.wait_for(kMainThreadTimeout) != std::future_status::ready) return std::nullopt;
    try {
        return future.get();
    } catch (...) {
        return std::nullopt;
    }
}

template <class Fn>
bool PollMainThread(Fn fn, std::chrono::milliseconds timeout = std::chrono::duration_cast<std::chrono::milliseconds>(kStateTimeout)) {
    const auto deadline = Clock::now() + timeout;
    do {
        const auto result = OnMainThread(fn);
        if (!result) return false;
        if (*result) return true;
        std::this_thread::sleep_for(kTickInterval);
    } while (Clock::now() < deadline);
    const auto result = OnMainThread(fn);
    return result && *result;
}

bool MenusClear() {
    const auto ui = RE::UI::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    return ui && controls && !ui->GameIsPaused() && !controls->blockPlayerInput;
}

std::optional<bool> StableWeaponDrawn(RE::PlayerCharacter* player) {
    if (!player) return std::nullopt;
    switch (player->AsActorState()->GetWeaponState()) {
    case RE::WEAPON_STATE::kDrawn:
        return true;
    case RE::WEAPON_STATE::kSheathed:
        return false;
    default:
        return std::nullopt;
    }
}

bool WeaponMatches(RE::PlayerCharacter* player, bool drawn) {
    const auto state = StableWeaponDrawn(player);
    return state && *state == drawn;
}

std::uint32_t EquippedWeaponFormID(RE::PlayerCharacter* player, bool left_hand) {
    if (!player) return 0;
    if (const auto form = player->GetEquippedObject(left_hand); form) {
        if (const auto weapon = form->As<RE::TESObjectWEAP>()) return weapon->GetFormID();
    }
    return 0;
}

EquippedWeaponFormIDs EquippedWeapons(RE::PlayerCharacter* player) {
    return {
        EquippedWeaponFormID(player, false),
        EquippedWeaponFormID(player, true)
    };
}

bool EquippedWeaponsMatch(RE::PlayerCharacter* player, const EquippedWeaponFormIDs& expected) {
    return EquippedWeapons(player) == expected;
}

float HorizontalDistanceSquared(const RE::NiPoint3& lhs, const RE::NiPoint3& rhs) {
    const auto dx = lhs.x - rhs.x;
    const auto dy = lhs.y - rhs.y;
    return dx * dx + dy * dy;
}

void InitializeButton(
    RE::ButtonEvent* event,
    std::uint32_t key,
    const RE::BSFixedString& user_event,
    float value,
    float held_duration) {
    event->Init(
        RE::INPUT_DEVICE::kKeyboard,
        static_cast<std::int32_t>(key),
        value,
        held_duration,
        user_event);
    event->next = nullptr;
}

bool DispatchThroughHook(
    RE::ButtonEvent* event,
    std::uint32_t key,
    const RE::BSFixedString& user_event,
    float value,
    float held_duration) {
    const auto dispatcher = DevBenchInput::GetDispatcher();
    if (!event || !dispatcher || key == RE::ControlMap::kInvalid) return false;
    InitializeButton(event, key, user_event, value, held_duration);
    RE::InputEvent* events[] = { event };
    Hooks::InputHook::thunk(dispatcher, events);
    return true;
}

CaseSession CaptureBaseline(const CaseConfig& config) {
    CaseSession session;
    session.config = config;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto control_map = RE::ControlMap::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    session.result.player_loaded = player && controls;
    if (!player || !controls || !control_map || !user_events) return session;

    const auto location = player->GetCurrentLocation();
    const auto cell = player->GetParentCell();
    session.result.riverwood_exterior =
        location && cell && !cell->IsInteriorCell() && location->GetFormID() == 0x00013163;
    session.result.menus_clear = MenusClear();
    session.result.dispatcher_captured = DevBenchInput::GetDispatcher() != nullptr;
    session.result.tudm_absent = !ModCompatibility::TUDM::is_installed;
    const auto weapon_state = StableWeaponDrawn(player);
    if (!session.result.riverwood_exterior || !session.result.menus_clear ||
        !session.result.dispatcher_captured || !session.result.tudm_absent ||
        controls->data.autoMove || !weapon_state) {
        return session;
    }

    session.original_sneaking = player->IsSneaking();
    session.original_running = controls->data.running;
    session.original_weapon_drawn = *weapon_state;
    session.equipped_weapon_form_ids = EquippedWeapons(player);
    session.result.equipped_weapon_present = session.equipped_weapon_form_ids.HasWeapon();
    session.result.baseline_captured = true;
    if (!session.result.equipped_weapon_present) return session;

    session.forward_key = control_map->GetMappedKey(user_events->forward, RE::INPUT_DEVICE::kKeyboard);
    session.sprint_key = control_map->GetMappedKey(user_events->sprint, RE::INPUT_DEVICE::kKeyboard);
    session.sneak_key = control_map->GetMappedKey(user_events->sneak, RE::INPUT_DEVICE::kKeyboard);
    session.ready_weapon_key = control_map->GetMappedKey(user_events->readyWeapon, RE::INPUT_DEVICE::kKeyboard);
    session.result.forward_mapping_valid = session.forward_key != RE::ControlMap::kInvalid;
    session.result.sprint_mapping_valid = session.sprint_key != RE::ControlMap::kInvalid;
    session.result.sneak_mapping_valid = session.sneak_key != RE::ControlMap::kInvalid;
    const auto ready_weapon_mapping_valid =
        session.ready_weapon_key != RE::ControlMap::kInvalid;
    session.result.movement_handler_ready =
        controls->movementHandler &&
        controls->movementHandler->IsInputEventHandlingEnabled() &&
        controls->readyWeaponHandler &&
        controls->readyWeaponHandler->IsInputEventHandlingEnabled();
    if (!session.result.forward_mapping_valid || !session.result.sprint_mapping_valid ||
        !session.result.sneak_mapping_valid || !ready_weapon_mapping_valid ||
        !session.result.movement_handler_ready) {
        return session;
    }
    session.forward_event =
        Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, user_events->forward, 0.0F, 0.0F);
    session.sprint_event =
        Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, user_events->sprint, 0.0F, 0.0F);
    session.sneak_event =
        Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, user_events->sneak, 0.0F, 0.0F);
    session.ready_weapon_event =
        Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, user_events->readyWeapon, 0.0F, 0.0F);
    session.result.forward_event_created = session.forward_event != nullptr;
    session.result.sprint_event_created = session.sprint_event != nullptr;
    session.result.eligible =
        session.result.forward_event_created &&
        session.result.sprint_event_created &&
        session.sneak_event &&
        session.ready_weapon_event;
    return session;
}

bool DispatchReadyWeaponToggle(CaseSession& session) {
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (!controls || !controls->readyWeaponHandler || !user_events ||
        !session.ready_weapon_event ||
        session.ready_weapon_key == RE::ControlMap::kInvalid) {
        return false;
    }
    InitializeButton(
        session.ready_weapon_event,
        session.ready_weapon_key,
        user_events->readyWeapon,
        1.0F,
        0.0F);
    const auto down = Hooks::SendButtonEvent(
        session.ready_weapon_event,
        controls->readyWeaponHandler);
    InitializeButton(
        session.ready_weapon_event,
        session.ready_weapon_key,
        user_events->readyWeapon,
        0.0F,
        kMinimumReleaseDuration);
    const auto up = Hooks::SendButtonEvent(
        session.ready_weapon_event,
        controls->readyWeaponHandler);
    return down && up;
}

CaseSession RequestWeaponSetup(CaseSession session) {
    if (Cancelled(session)) return session;
    const auto player = RE::PlayerCharacter::GetSingleton();
    if (player && session.result.eligible && !WeaponMatches(player, session.config.weapons_drawn)) {
        DispatchReadyWeaponToggle(session);
    }
    return session;
}

bool WeaponSetupReady(const CaseSession& session) {
    if (Cancelled(session)) return false;
    const auto player = RE::PlayerCharacter::GetSingleton();
    return player && MenusClear() &&
        WeaponMatches(player, session.config.weapons_drawn) &&
        EquippedWeaponsMatch(player, session.equipped_weapon_form_ids);
}

CaseSession RequestWalkingSetup(CaseSession session) {
    if (Cancelled(session)) return session;
    const auto controls = RE::PlayerControls::GetSingleton();
    if (controls && session.result.eligible &&
        (!controls->data.running) != session.config.walking) {
        Hooks::SendToggleRunEvent(RE::INPUT_DEVICE::kKeyboard);
    }
    return session;
}

bool WalkingSetupReady(const CaseSession& session) {
    if (Cancelled(session)) return false;
    const auto controls = RE::PlayerControls::GetSingleton();
    return controls && MenusClear() && (!controls->data.running) == session.config.walking;
}

CaseSession RequestSneakSetup(CaseSession session) {
    if (Cancelled(session)) return session;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (player && user_events && session.result.eligible && !player->IsSneaking()) {
        session.sneak_owned = DispatchThroughHook(
            session.sneak_event,
            session.sneak_key,
            user_events->sneak,
            1.0F,
            0.0F);
    }
    return session;
}

CaseSession ReleaseSneakSetup(CaseSession session) {
    if (Cancelled(session)) return session;
    const auto user_events = RE::UserEvents::GetSingleton();
    if (user_events && session.sneak_owned) {
        if (DispatchThroughHook(
                session.sneak_event,
                session.sneak_key,
                user_events->sneak,
                0.0F,
                kMinimumReleaseDuration)) {
            session.sneak_owned = false;
        }
    }
    return session;
}

bool SneakSetupReady(const CaseSession& session) {
    if (Cancelled(session)) return false;
    const auto player = RE::PlayerCharacter::GetSingleton();
    return player && MenusClear() && player->IsSneaking() &&
        WeaponMatches(player, session.config.weapons_drawn) &&
        EquippedWeaponsMatch(player, session.equipped_weapon_form_ids);
}

PositionSample CapturePositionIfNeutral(const CaseSession& session) {
    PositionSample sample;
    if (Cancelled(session)) return sample;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    if (!player || !controls || !MenusClear() || controls->data.autoMove ||
        controls->data.moveInputVec.SqrLength() > kMoveInputEpsilonSquared ||
        player->IsMoving()) {
        return sample;
    }
    sample.valid = true;
    sample.position = player->GetPosition();
    return sample;
}

bool StillStationary(const CaseSession& session, const PositionSample& sample) {
    if (Cancelled(session)) return false;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    return sample.valid && player && controls && MenusClear() &&
        !controls->data.autoMove &&
        controls->data.moveInputVec.SqrLength() <= kMoveInputEpsilonSquared &&
        !player->IsMoving() &&
        HorizontalDistanceSquared(player->GetPosition(), sample.position) <= kMaximumStationaryDriftSquared;
}

bool WaitForStationaryBaseline(const CaseSession& session) {
    const auto deadline = Clock::now() + kStateTimeout;
    do {
        const auto sample = OnMainThread([session] { return CapturePositionIfNeutral(session); });
        if (!sample) return false;
        if (sample->valid) {
            std::this_thread::sleep_for(kStationarySampleInterval);
            const auto stable = OnMainThread([session, snapshot = *sample] { return StillStationary(session, snapshot); });
            if (!stable) return false;
            if (*stable) return true;
        } else {
            std::this_thread::sleep_for(kTickInterval);
        }
    } while (Clock::now() < deadline);
    return false;
}

bool NativeExpectedWeaponsMatch(
    RE::PlayerCharacter* player,
    const DevBenchSneakMatrix::NativeInputSession& native) {
    return player &&
        EquippedWeaponFormID(player, false) == native.expected_right_weapon &&
        EquippedWeaponFormID(player, true) == native.expected_left_weapon;
}

void SampleNativeInputLocked(DevBenchSneakMatrix::NativeInputSession& native) {
    if (native.cancelled.load() || native.phase == DevBenchSneakMatrix::NativePumpPhase::kRelease ||
        native.phase == DevBenchSneakMatrix::NativePumpPhase::kDone ||
        native.phase == DevBenchSneakMatrix::NativePumpPhase::kFailed) {
        return;
    }
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    if (!player || !controls || !MenusClear() || controls->data.autoMove) {
        native.phase = DevBenchSneakMatrix::NativePumpPhase::kFailed;
        native.error = "native input pump lost a valid gameplay state";
        return;
    }

    const auto move_input = controls->data.moveInputVec.SqrLength() > kMoveInputEpsilonSquared;
    native.move_input_observed = native.move_input_observed || move_input;
    native.actor_walking_setup =
        player->AsActorState()->IsWalking() == native.walking;
    const auto setup_valid =
        player->IsSneaking() &&
        (!controls->data.running) == native.walking &&
        native.actor_walking_setup &&
        WeaponMatches(player, native.weapons_drawn) &&
        NativeExpectedWeaponsMatch(player, native);
    const auto displaced =
        HorizontalDistanceSquared(player->GetPosition(), native.movement_origin) >=
        kMinimumHorizontalMovementSquared;

    if (native.phase == DevBenchSneakMatrix::NativePumpPhase::kForward &&
        native.forward_held_ticks >= 2 && native.move_input_observed &&
        player->IsMoving() && displaced && setup_valid) {
        native.movement_observed = true;
        native.sprint_origin = player->GetPosition();
        native.sprint_started = Clock::now();
        native.phase = DevBenchSneakMatrix::NativePumpPhase::kSprint;
    }

    if (native.phase == DevBenchSneakMatrix::NativePumpPhase::kSprint &&
        native.sprint_sequence_complete) {
        native.phase = DevBenchSneakMatrix::NativePumpPhase::kPostSprint;
    }

    if (native.phase == DevBenchSneakMatrix::NativePumpPhase::kPostSprint) {
        native.sneaking_after = player->IsSneaking();
        native.running_after = controls->data.running;
        native.actor_walking_after = player->AsActorState()->IsWalking();
        native.moving_after = player->IsMoving();
        native.continued_displacement =
            HorizontalDistanceSquared(player->GetPosition(), native.sprint_origin) >=
            kMinimumHorizontalMovementSquared;
        native.weapon_matches_after = WeaponMatches(player, native.weapons_drawn);
        native.equipped_weapons_match_after = NativeExpectedWeaponsMatch(player, native);
        const auto valid_post_frame =
            !native.sneaking_after && native.running_after &&
            !native.actor_walking_after && native.moving_after && move_input &&
            native.weapon_matches_after && native.equipped_weapons_match_after;
        if (valid_post_frame) {
            ++native.post_sprint_forward_ticks;
        }
        native.outcome_observed =
            native.post_sprint_forward_ticks >= 2 &&
            native.continued_displacement;
        if (native.outcome_observed) {
            native.phase = DevBenchSneakMatrix::NativePumpPhase::kDone;
        }
    }
}

CaseSession ArmNativeInput(CaseSession session) {
    auto native = std::make_shared<DevBenchSneakMatrix::NativeInputSession>();
    native->walking = session.config.walking;
    native->weapons_drawn = session.config.weapons_drawn;
    native->expected_right_weapon = session.equipped_weapon_form_ids.right;
    native->expected_left_weapon = session.equipped_weapon_form_ids.left;
    native->forward_event = session.forward_event;
    native->sprint_event = session.sprint_event;
    native->forward_key = session.forward_key;
    native->sprint_key = session.sprint_key;
    {
        std::lock_guard lock(g_native_session_mutex);
        g_native_session = native;
    }
    session.native_input = std::move(native);
    return session;
}

CaseSession SyncNativeInput(CaseSession session) {
    const auto native = session.native_input;
    if (!native) return session;
    std::lock_guard lock(native->mutex);
    session.forward_held_seconds = native->forward_held_seconds;
    session.sprint_held_seconds = native->sprint_held_seconds;
    session.forward_owned = native->forward_owned;
    session.sprint_owned = native->sprint_owned;
    session.result.forward_down_dispatched = native->forward_down_dispatched;
    session.result.forward_held_ticks = native->forward_held_ticks;
    session.result.move_input_observed = native->move_input_observed;
    session.result.movement_observed = native->movement_observed;
    session.result.actor_walking_setup = native->actor_walking_setup;
    session.result.sprint_down_dispatched = native->sprint_down_dispatched;
    session.result.sprint_held_ticks = native->sprint_held_ticks;
    session.result.sprint_held_dispatched = native->sprint_held_dispatched;
    session.result.sprint_sequence_dispatched = native->sprint_sequence_dispatched;
    session.result.sprint_sequence_complete = native->sprint_sequence_complete;
    session.result.post_sprint_forward_ticks = native->post_sprint_forward_ticks;
    session.result.outcome_observed = native->outcome_observed;
    session.result.sneaking_after = native->sneaking_after;
    session.result.running_after = native->running_after;
    session.result.actor_walking_after = native->actor_walking_after;
    session.result.moving_after = native->moving_after;
    session.result.continued_displacement = native->continued_displacement;
    session.result.weapon_matches_after = native->weapon_matches_after;
    session.result.equipped_weapons_match_after = native->equipped_weapons_match_after;
    session.result.sprint_up_dispatched = native->sprint_up_dispatched;
    session.result.forward_up_dispatched = native->forward_up_dispatched;
    if (session.result.error.empty() && !native->error.empty()) {
        session.result.error = native->error;
    }
    return session;
}

bool NativeInputFinished(const CaseSession& session) {
    const auto native = session.native_input;
    if (!native) return true;
    std::lock_guard lock(native->mutex);
    return native->outcome_observed ||
        native->phase == DevBenchSneakMatrix::NativePumpPhase::kFailed;
}

CaseSession ReleaseNativeSprintDirect(CaseSession session) {
    const auto native = session.native_input;
    if (!native) return session;
    native->cancelled.store(true);

    bool sprint_owned = false;
    float held_seconds = 0.0F;
    {
        std::lock_guard lock(native->mutex);
        sprint_owned = native->sprint_owned;
        held_seconds = native->sprint_held_seconds;
        if (!sprint_owned) {
            native->sprint_up_dispatched = true;
            session.result.sprint_up_dispatched = true;
            session.sprint_owned = false;
            return session;
        }
    }

    const auto controls = RE::PlayerControls::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (!controls || !controls->sprintHandler || !user_events || !session.sprint_event) {
        return session;
    }
    InitializeButton(
        session.sprint_event,
        session.sprint_key,
        user_events->sprint,
        0.0F,
        std::max(held_seconds, kMinimumReleaseDuration));
    const auto released =
        Hooks::SendButtonEvent(session.sprint_event, controls->sprintHandler);
    {
        std::lock_guard lock(native->mutex);
        native->sprint_up_dispatched = released;
        if (released) native->sprint_owned = false;
    }
    session.result.sprint_up_dispatched = released;
    if (released) session.sprint_owned = false;
    return session;
}
void ArmNativeRelease(const CaseSession& session) {
    const auto native = session.native_input;
    if (!native) return;
    native->cancelled.store(true);
    std::lock_guard lock(native->mutex);
    native->release_requested = true;
    native->phase = DevBenchSneakMatrix::NativePumpPhase::kRelease;
    if (!native->forward_owned) {
        native->forward_up_dispatched = true;
        native->release_acknowledged = true;
    }
}

bool NativeReleaseAcknowledged(const CaseSession& session) {
    const auto native = session.native_input;
    if (!native) return true;
    std::lock_guard lock(native->mutex);
    return native->release_acknowledged;
}

void DetachNativeInput(const CaseSession& session) {
    const auto native = session.native_input;
    if (!native) return;
    native->cancelled.store(true);
    std::lock_guard lock(g_native_session_mutex);
    if (g_native_session == native) g_native_session.reset();
}
CaseSession ReleaseInputs(CaseSession session) {
    session.result.cleanup_attempted = true;
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (!controls || !user_events) return session;

    if (session.sneak_owned && session.sneak_event) {
        auto sneak_released = DispatchThroughHook(
            session.sneak_event,
            session.sneak_key,
            user_events->sneak,
            0.0F,
            kMinimumReleaseDuration);
        if (!sneak_released && controls->sneakHandler) {
            InitializeButton(
                session.sneak_event,
                session.sneak_key,
                user_events->sneak,
                0.0F,
                kMinimumReleaseDuration);
            sneak_released = Hooks::SendButtonEvent(
                session.sneak_event,
                controls->sneakHandler);
        }
        if (sneak_released) session.sneak_owned = false;
    }

    if (session.sprint_owned && session.sprint_event) {
        InitializeButton(
            session.sprint_event,
            session.sprint_key,
            user_events->sprint,
            0.0F,
            std::max(session.sprint_held_seconds, kMinimumReleaseDuration));
        session.result.sprint_up_dispatched =
            Hooks::SendButtonEvent(session.sprint_event, controls->sprintHandler);
        if (session.result.sprint_up_dispatched) session.sprint_owned = false;
    } else {
        session.result.sprint_up_dispatched = true;
    }

    if (session.forward_owned && session.forward_event) {
        const auto release_duration =
            std::max(session.forward_held_seconds, kMinimumReleaseDuration);
        InitializeButton(
            session.forward_event,
            session.forward_key,
            user_events->forward,
            0.0F,
            release_duration);
        session.result.forward_up_dispatched =
            controls->movementHandler &&
            Hooks::SendButtonEvent(session.forward_event, controls->movementHandler);
        session.result.forward_up_fallback = session.result.forward_up_dispatched;
        if (session.result.forward_up_dispatched) session.forward_owned = false;
    } else {
        session.result.forward_up_dispatched = true;
    }
    return session;
}

bool MoveInputNeutral() {
    const auto controls = RE::PlayerControls::GetSingleton();
    return controls &&
        !controls->data.autoMove &&
        controls->data.moveInputVec.SqrLength() <= kMoveInputEpsilonSquared;
}

CaseSession RetryForwardUpDirect(CaseSession session) {
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (!controls || !controls->movementHandler || !user_events || !session.forward_event) {
        return session;
    }
    InitializeButton(
        session.forward_event,
        session.forward_key,
        user_events->forward,
        0.0F,
        std::max(session.forward_held_seconds, kMinimumReleaseDuration));
    session.result.forward_up_direct_retry =
        Hooks::SendButtonEvent(session.forward_event, controls->movementHandler);
    session.result.forward_up_dispatched =
        session.result.forward_up_dispatched || session.result.forward_up_direct_retry;
    return session;
}

PositionSample CaptureCleanupPosition() {
    PositionSample sample;
    const auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !MoveInputNeutral() || player->IsMoving()) return sample;
    sample.valid = true;
    sample.position = player->GetPosition();
    return sample;
}

bool CleanupPositionMatches(const PositionSample& sample) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    return sample.valid && player && MoveInputNeutral() && !player->IsMoving() &&
        HorizontalDistanceSquared(player->GetPosition(), sample.position) <=
        kMaximumStationaryDriftSquared;
}

bool WaitForStableCleanupPosition() {
    const auto deadline = Clock::now() + kStateTimeout;
    do {
        const auto sample = OnMainThread([] { return CaptureCleanupPosition(); });
        if (!sample) return false;
        if (sample->valid) {
            std::this_thread::sleep_for(kStationarySampleInterval);
            const auto first =
                OnMainThread([snapshot = *sample] { return CleanupPositionMatches(snapshot); });
            if (!first) return false;
            if (*first) {
                std::this_thread::sleep_for(kStationarySampleInterval);
                const auto second =
                    OnMainThread([snapshot = *sample] { return CleanupPositionMatches(snapshot); });
                if (!second) return false;
                if (*second) return true;
            }
        }
        std::this_thread::sleep_for(kTickInterval);
    } while (Clock::now() < deadline);
    return false;
}

CaseSession RequestWeaponRestore(CaseSession session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    if (player && session.result.baseline_captured &&
        !WeaponMatches(player, session.original_weapon_drawn)) {
        DispatchReadyWeaponToggle(session);
    }
    return session;
}

bool WeaponRestoreReady(const CaseSession& session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    return player &&
        WeaponMatches(player, session.original_weapon_drawn) &&
        EquippedWeaponsMatch(player, session.equipped_weapon_form_ids);
}

CaseSession RequestRunningRestore(CaseSession session) {
    const auto controls = RE::PlayerControls::GetSingleton();
    if (controls && session.result.baseline_captured &&
        controls->data.running != session.original_running) {
        Hooks::SendToggleRunEvent(RE::INPUT_DEVICE::kKeyboard);
    }
    return session;
}

bool RunningRestoreReady(const CaseSession& session) {
    const auto controls = RE::PlayerControls::GetSingleton();
    return controls && controls->data.running == session.original_running;
}

CaseSession RequestSneakRestore(CaseSession session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    if (player && user_events && session.result.baseline_captured &&
        player->IsSneaking() != session.original_sneaking) {
        session.sneak_owned = DispatchThroughHook(
            session.sneak_event,
            session.sneak_key,
            user_events->sneak,
            1.0F,
            0.0F);
    }
    return session;
}

CaseSession ReleaseSneakRestore(CaseSession session) {
    const auto user_events = RE::UserEvents::GetSingleton();
    if (user_events && session.sneak_owned) {
        if (DispatchThroughHook(
                session.sneak_event,
                session.sneak_key,
                user_events->sneak,
                0.0F,
                kMinimumReleaseDuration)) {
            session.sneak_owned = false;
        }
    }
    return session;
}

bool SneakRestoreReady(const CaseSession& session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    return player && player->IsSneaking() == session.original_sneaking;
}

CaseSession CaptureCleanup(
    CaseSession session,
    bool input_released,
    bool stable_position,
    bool weapon_restored,
    bool running_restored,
    bool sneak_restored) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    if (!player || !controls) return session;
    session.result.auto_move_after_cleanup = controls->data.autoMove;
    session.result.move_input_neutral =
        controls->data.moveInputVec.SqrLength() <= kMoveInputEpsilonSquared &&
        !session.result.auto_move_after_cleanup;
    session.result.movement_stopped = !player->IsMoving();
    session.result.stable_position_after_cleanup = stable_position;
    session.result.cleanup_weapon_restored =
        weapon_restored && WeaponRestoreReady(session);
    session.result.cleanup_running_restored =
        running_restored && RunningRestoreReady(session);
    session.result.cleanup_sneaking_restored =
        sneak_restored && SneakRestoreReady(session);
    session.result.cleanup_equipment_restored =
        EquippedWeaponsMatch(player, session.equipped_weapon_form_ids);
    session.result.cleanup_settled =
        input_released &&
        session.result.stable_position_after_cleanup &&
        !session.result.auto_move_after_cleanup &&
        session.result.sprint_up_dispatched &&
        session.result.forward_up_dispatched &&
        session.result.move_input_neutral &&
        session.result.movement_stopped &&
        session.result.cleanup_weapon_restored &&
        session.result.cleanup_running_restored &&
        session.result.cleanup_sneaking_restored &&
        session.result.cleanup_equipment_restored;
    session.result.ok = session.result.outcome_observed && session.result.cleanup_settled;
    return session;
}

void QueueEmergencyRelease(CaseSession session) {
    const auto tasks = SKSE::GetTaskInterface();
    if (!tasks) return;
    tasks->AddTask([session]() mutable {
        ReleaseInputs(session);
    });
}

CaseSession CleanupSession(CaseSession session) {
    if (session.cancelled) session.cancelled->store(true);
    if (session.native_input) {
        session.native_input->cancelled.store(true);
        const auto sprint_release =
            OnMainThread([session] { return ReleaseNativeSprintDirect(session); });
        if (sprint_release) session = *sprint_release;

        ArmNativeRelease(session);
        const auto release_deadline = Clock::now() + kInitialReleaseTimeout;
        while (Clock::now() < release_deadline && !NativeReleaseAcknowledged(session)) {
            std::this_thread::sleep_for(kTickInterval);
        }
        session = SyncNativeInput(std::move(session));
        if (NativeReleaseAcknowledged(session)) {
            session.forward_owned = false;
        }
        DetachNativeInput(session);
    }
    const auto released = OnMainThread([session] { return ReleaseInputs(session); });
    if (!released) {
        QueueEmergencyRelease(session);
        session.result.error = "input release task timed out; emergency release queued";
        session.result.ok = false;
        return session;
    }
    session = *released;

    auto input_released = PollMainThread(
        [] { return MoveInputNeutral(); },
        kInitialReleaseTimeout);
    if (!input_released) {
        const auto retried =
            OnMainThread([session] { return RetryForwardUpDirect(session); });
        if (retried) session = *retried;
        input_released = PollMainThread([] { return MoveInputNeutral(); });
    }
    const auto stable_position = WaitForStableCleanupPosition();

    const auto weapon_request =
        OnMainThread([session] { return RequestWeaponRestore(session); });
    if (weapon_request) session = *weapon_request;
    const auto weapon_restored =
        weapon_request && PollMainThread([session] { return WeaponRestoreReady(session); });

    const auto running_request =
        OnMainThread([session] { return RequestRunningRestore(session); });
    if (running_request) session = *running_request;
    const auto running_restored =
        running_request && PollMainThread([session] { return RunningRestoreReady(session); });

    const auto sneak_request =
        OnMainThread([session] { return RequestSneakRestore(session); });
    if (sneak_request) session = *sneak_request;
    if (sneak_request && session.sneak_owned) {
        std::this_thread::sleep_for(kSneakEventInterval);
        const auto sneak_release =
            OnMainThread([session] { return ReleaseSneakRestore(session); });
        if (sneak_release) session = *sneak_release;
    }
    const auto sneak_restored =
        sneak_request && PollMainThread([session] { return SneakRestoreReady(session); });

    const auto captured = OnMainThread([
        session,
        input_released,
        stable_position,
        weapon_restored,
        running_restored,
        sneak_restored] {
        return CaptureCleanup(
            session,
            input_released,
            stable_position,
            weapon_restored,
            running_restored,
            sneak_restored);
    });
    if (captured) return *captured;
    session.result.ok = false;
    if (session.result.error.empty()) session.result.error = "cleanup verification task timed out";
    return session;
}

std::string Serialize(const CaseSession& session) {
    const auto& r = session.result;
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    return std::string{"{\"ok\":"} + flag(r.ok) +
        ",\"error\":\"" + r.error + "\"" +
        ",\"desiredWalking\":" + flag(session.config.walking) +
        ",\"desiredWeaponsDrawn\":" + flag(session.config.weapons_drawn) +
        ",\"playerLoaded\":" + flag(r.player_loaded) +
        ",\"riverwoodExterior\":" + flag(r.riverwood_exterior) +
        ",\"menusClear\":" + flag(r.menus_clear) +
        ",\"dispatcherCaptured\":" + flag(r.dispatcher_captured) +
        ",\"tudmAbsent\":" + flag(r.tudm_absent) +
        ",\"baselineCaptured\":" + flag(r.baseline_captured) +
        ",\"eligible\":" + flag(r.eligible) +
        ",\"equippedWeaponPresent\":" + flag(r.equipped_weapon_present) +
        ",\"forwardMappingValid\":" + flag(r.forward_mapping_valid) +
        ",\"sprintMappingValid\":" + flag(r.sprint_mapping_valid) +
        ",\"sneakMappingValid\":" + flag(r.sneak_mapping_valid) +
        ",\"movementHandlerReady\":" + flag(r.movement_handler_ready) +
        ",\"setupWeapon\":" + flag(r.setup_weapon) +
        ",\"setupWalking\":" + flag(r.setup_walking) +
        ",\"setupSneaking\":" + flag(r.setup_sneaking) +
        ",\"stationaryBaseline\":" + flag(r.stationary_baseline) +
        ",\"forwardDownDispatched\":" + flag(r.forward_down_dispatched) +
        ",\"forwardHeldTicks\":" + std::to_string(r.forward_held_ticks) +
        ",\"moveInputObserved\":" + flag(r.move_input_observed) +
        ",\"movementObserved\":" + flag(r.movement_observed) +
        ",\"actorWalkingSetup\":" + flag(r.actor_walking_setup) +
        ",\"sprintDownDispatched\":" + flag(r.sprint_down_dispatched) +
        ",\"sprintHeldDispatched\":" + flag(r.sprint_held_dispatched) +
        ",\"sprintHeldTicks\":" + std::to_string(r.sprint_held_ticks) +
        ",\"sprintSequenceComplete\":" + flag(r.sprint_sequence_complete) +
        ",\"postSprintForwardTicks\":" + std::to_string(r.post_sprint_forward_ticks) +
        ",\"outcomeObserved\":" + flag(r.outcome_observed) +
        ",\"sneakingAfter\":" + flag(r.sneaking_after) +
        ",\"runningAfter\":" + flag(r.running_after) +
        ",\"actorWalkingAfter\":" + flag(r.actor_walking_after) +
        ",\"movingAfter\":" + flag(r.moving_after) +
        ",\"continuedDisplacement\":" + flag(r.continued_displacement) +
        ",\"weaponMatchesAfter\":" + flag(r.weapon_matches_after) +
        ",\"equippedWeaponsMatchAfter\":" + flag(r.equipped_weapons_match_after) +
        ",\"cleanupAttempted\":" + flag(r.cleanup_attempted) +
        ",\"sprintUpDispatched\":" + flag(r.sprint_up_dispatched) +
        ",\"forwardUpDispatched\":" + flag(r.forward_up_dispatched) +
        ",\"forwardUpFallback\":" + flag(r.forward_up_fallback) +
        ",\"forwardUpDirectRetry\":" + flag(r.forward_up_direct_retry) +
        ",\"moveInputNeutral\":" + flag(r.move_input_neutral) +
        ",\"autoMoveAfterCleanup\":" + flag(r.auto_move_after_cleanup) +
        ",\"stablePositionAfterCleanup\":" + flag(r.stable_position_after_cleanup) +
        ",\"movementStopped\":" + flag(r.movement_stopped) +
        ",\"cleanupWeaponRestored\":" + flag(r.cleanup_weapon_restored) +
        ",\"cleanupRunningRestored\":" + flag(r.cleanup_running_restored) +
        ",\"cleanupSneakingRestored\":" + flag(r.cleanup_sneaking_restored) +
        ",\"cleanupEquipmentRestored\":" + flag(r.cleanup_equipment_restored) +
        ",\"cleanupSettled\":" + flag(r.cleanup_settled) + "}";
}

void WriteResult(CaseSession session, void* sink, DevBenchAPI::WriteFn write) {
    session = CleanupSession(std::move(session));
    const auto json = Serialize(session);
    write(sink, json.c_str());
}

void RunCase(void* context, const char*, void* sink, DevBenchAPI::WriteFn write) {
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) {
        write(sink, R"({"ok":false,"error":"another sprint matrix case is already running"})");
        return;
    }
    struct BusyReset {
        ~BusyReset() { g_busy.store(false); }
    } reset;

    const auto config = *static_cast<const CaseConfig*>(context);
    const auto baseline = OnMainThread([config] { return CaptureBaseline(config); });
    if (!baseline) {
        write(sink, R"({"ok":false,"error":"baseline capture task timed out"})");
        return;
    }
    auto session = *baseline;
    if (!session.result.eligible) {
        session.result.error = "baseline is not eligible for the held-Forward matrix";
        WriteResult(std::move(session), sink, write);
        return;
    }

    const auto weapon_request =
        OnMainThread([session] { return RequestWeaponSetup(session); });
    if (!weapon_request) {
        session.result.error = "weapon setup task timed out";
        WriteResult(std::move(session), sink, write);
        return;
    }
    session = *weapon_request;
    session.result.setup_weapon =
        PollMainThread([session] { return WeaponSetupReady(session); });
    if (!session.result.setup_weapon) {
        session.result.error = "weapon state did not converge";
        WriteResult(std::move(session), sink, write);
        return;
    }

    const auto walking_request =
        OnMainThread([session] { return RequestWalkingSetup(session); });
    if (!walking_request) {
        session.result.error = "walk/run setup task timed out";
        WriteResult(std::move(session), sink, write);
        return;
    }
    session = *walking_request;
    session.result.setup_walking =
        PollMainThread([session] { return WalkingSetupReady(session); });
    if (!session.result.setup_walking) {
        session.result.error = "walk/run state did not converge";
        WriteResult(std::move(session), sink, write);
        return;
    }

    const auto sneak_request =
        OnMainThread([session] { return RequestSneakSetup(session); });
    if (!sneak_request) {
        session.result.error = "sneak setup task timed out";
        WriteResult(std::move(session), sink, write);
        return;
    }
    session = *sneak_request;
    if (session.sneak_owned) {
        std::this_thread::sleep_for(kSneakEventInterval);
        const auto sneak_release =
            OnMainThread([session] { return ReleaseSneakSetup(session); });
        if (!sneak_release) {
            session.result.error = "sneak setup release task timed out";
            WriteResult(std::move(session), sink, write);
            return;
        }
        session = *sneak_release;
    }
    session.result.setup_sneaking =
        PollMainThread([session] { return SneakSetupReady(session); });
    if (!session.result.setup_sneaking) {
        session.result.error = "sneak state did not converge";
        WriteResult(std::move(session), sink, write);
        return;
    }

    session.result.stationary_baseline = WaitForStationaryBaseline(session);
    if (!session.result.stationary_baseline) {
        session.result.error = "player did not reach a neutral stationary baseline";
        WriteResult(std::move(session), sink, write);
        return;
    }

    session = ArmNativeInput(std::move(session));
    const auto native_deadline =
        Clock::now() + kMovementTimeout + kOutcomeTimeout + kOutcomeTimeout;
    while (Clock::now() < native_deadline && !NativeInputFinished(session)) {
        std::this_thread::sleep_for(kTickInterval);
        session = SyncNativeInput(std::move(session));
    }
    session = SyncNativeInput(std::move(session));
    if (!session.result.outcome_observed && session.result.error.empty()) {
        if (!session.result.forward_down_dispatched) {
            session.result.error = "native input pump did not receive an input batch";
        } else if (!session.result.movement_observed) {
            session.result.error = "native Forward input did not produce the requested moving gait";
        } else if (!session.result.sprint_sequence_complete) {
            session.result.error = "native Sprint held sequence did not exceed 0.50 seconds";
        } else {
            session.result.error = "native Sprint did not produce the required sneak/run/weapon outcome";
        }
    }
    WriteResult(std::move(session), sink, write);
}
}
DevBenchSneakMatrix::NativeInputFrame DevBenchSneakMatrix::BeginNativeInputFrame(
    RE::InputEvent* original_head) {
    NativeInputFrame frame;
    frame.head = original_head;
    frame.original_head = original_head;
    {
        std::lock_guard lock(g_native_session_mutex);
        frame.session = g_native_session;
    }
    const auto native = frame.session;
    if (!native) return frame;

    std::lock_guard lock(native->mutex);
    if (native->in_frame) return frame;

    auto prepend = [&](RE::ButtonEvent* event, RE::InputEvent*& head, RE::InputEvent*& tail) {
        if (!head) head = event;
        if (tail) tail->next = event;
        tail = event;
        event->next = nullptr;
    };

    RE::InputEvent* synthetic_head = nullptr;
    RE::InputEvent* synthetic_tail = nullptr;
    const auto user_events = RE::UserEvents::GetSingleton();
    if (!user_events) return frame;

    if (native->release_requested) {
        frame.release_frame = true;
        if (native->forward_owned && native->forward_event) {
            InitializeButton(
                native->forward_event,
                native->forward_key,
                user_events->forward,
                0.0F,
                std::max(native->forward_held_seconds, kMinimumReleaseDuration));
            prepend(native->forward_event, synthetic_head, synthetic_tail);
        }
    } else {
        if (native->cancelled.load() ||
            native->phase == NativePumpPhase::kFailed) {
            return frame;
        }
        SampleNativeInputLocked(*native);
        if (native->phase == NativePumpPhase::kFailed) return frame;

        const auto now = Clock::now();
        if (!native->forward_owned) {
            native->movement_origin = RE::PlayerCharacter::GetSingleton()->GetPosition();
            native->forward_started = now;
            native->forward_held_seconds = 0.0F;
            InitializeButton(
                native->forward_event,
                native->forward_key,
                user_events->forward,
                1.0F,
                0.0F);
            native->forward_owned = true;
            frame.forward_down = true;
        } else {
            frame.forward_held = true;
            native->forward_held_seconds =
                std::max(
                    std::chrono::duration<float>(now - native->forward_started).count(),
                    kMinimumReleaseDuration);
            InitializeButton(
                native->forward_event,
                native->forward_key,
                user_events->forward,
                1.0F,
                native->forward_held_seconds);
        }
        prepend(native->forward_event, synthetic_head, synthetic_tail);

        if (native->phase == NativePumpPhase::kSprint) {
            if (!native->sprint_owned) {
                native->sprint_started = now;
                native->sprint_held_seconds = 0.0F;
                InitializeButton(
                    native->sprint_event,
                    native->sprint_key,
                    user_events->sprint,
                    1.0F,
                    0.0F);
                native->sprint_owned = true;
                frame.sprint_down = true;
            } else {
                frame.sprint_held = true;
                native->sprint_held_seconds =
                    std::max(
                        std::chrono::duration<float>(now - native->sprint_started).count(),
                        kMinimumReleaseDuration);
                InitializeButton(
                    native->sprint_event,
                    native->sprint_key,
                    user_events->sprint,
                    1.0F,
                    native->sprint_held_seconds);
            }
            prepend(native->sprint_event, synthetic_head, synthetic_tail);
        }
    }

    if (!synthetic_head) return frame;
    synthetic_tail->next = original_head;
    native->in_frame = true;
    frame.head = synthetic_head;
    frame.injected = true;
    return frame;
}

void DevBenchSneakMatrix::EndNativeInputFrame(NativeInputFrame& frame) {
    const auto native = frame.session;
    if (!native || !frame.injected) return;
    std::lock_guard lock(native->mutex);
    if (native->forward_event) native->forward_event->next = nullptr;
    if (native->sprint_event) native->sprint_event->next = nullptr;

    if (frame.release_frame) {
        native->forward_up_dispatched = true;
        native->forward_owned = false;
        native->release_acknowledged = true;
        native->phase = NativePumpPhase::kDone;
    } else {
        if (frame.forward_down) native->forward_down_dispatched = true;
        if (frame.forward_held) ++native->forward_held_ticks;
        if (frame.sprint_down) native->sprint_down_dispatched = true;
        if (frame.sprint_held) {
            ++native->sprint_held_ticks;
            native->sprint_held_dispatched = true;
            if (native->sprint_held_seconds > kRequiredSprintHeldSeconds) {
                native->sprint_sequence_complete = true;
                native->sprint_sequence_dispatched =
                    native->sprint_down_dispatched && native->sprint_held_dispatched;
            }
        }
    }
    native->in_frame = false;
    frame.injected = false;
}

void DevBenchSneakMatrix::Register() {
    const auto api = DevBenchAPI::GetDevBenchInterface001();
    if (!api) {
        logger::info("DevBench is not installed; sprint matrix tools not registered");
        return;
    }
    for (const auto& test_case : kCases) {
        constexpr auto descriptor =
            R"({"description":"Runs one deterministic SprintCancelsSneak case using held Forward input and production hook dispatch.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"readOnly":false})";
        api->RegisterTool(
            test_case.tool_name,
            descriptor,
            RunCase,
            const_cast<CaseConfig*>(&test_case));
    }
    logger::info("Registered held-Forward sprint regression matrix tools");
}

