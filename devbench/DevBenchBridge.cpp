#include "DevBenchBridge.h"
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include "DevBenchAPI.h"
#include "DevBenchInput.h"
#include "Hooks.h"
#include "logger.h"

namespace {
constexpr auto kMovementSettleTime = std::chrono::milliseconds(2000);
constexpr auto kSprintHeldTime = std::chrono::milliseconds(250);
constexpr auto kSprintFrameTime = std::chrono::milliseconds(50);
constexpr std::size_t kSprintFrameCount = 20;
constexpr auto kMainThreadTimeout = std::chrono::seconds(3);
constexpr float kMinimumMovementDistanceSquared = 1.0F;

struct WalkSprintRegressionResult {
    bool player_loaded = false;
    bool riverwood_exterior = false;
    bool not_sneaking = false;
    bool menus_clear = false;
    bool auto_move_was_active = false;
    bool auto_move_enabled_by_test = false;
    bool movement_observed = false;
    bool moving = false;
    bool dispatcher_captured = false;
    bool walking_before = false;
    bool sprint_dispatched = false;
    bool walking_after = false;
    bool sprinting_after = false;
    bool sprint_up_dispatched = false;
    bool cleanup_attempted = false;
    bool auto_move_after_cleanup = false;
    bool passed = false;
};

struct RegressionSession {
    WalkSprintRegressionResult test;
    RE::NiPoint3 initial_position{};
};

RegressionSession BeginRegression() {
    RegressionSession session;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto ui = RE::UI::GetSingleton();
    session.test.player_loaded = player && controls;
    if (!player || !controls || !ui) return session;

    const auto location = player->GetCurrentLocation();
    const auto cell = player->GetParentCell();
    session.test.riverwood_exterior = location && cell && !cell->IsInteriorCell() && location->GetFormID() == 0x00013163;
    session.test.not_sneaking = !player->IsSneaking();
    session.test.menus_clear = !ui->GameIsPaused() && !controls->blockPlayerInput;
    session.test.auto_move_was_active = controls->data.autoMove;
    session.test.dispatcher_captured = DevBenchInput::GetDispatcher() != nullptr;
    if (!session.test.riverwood_exterior || !session.test.not_sneaking || !session.test.menus_clear ||
        session.test.auto_move_was_active || !session.test.dispatcher_captured || !controls->autoMoveHandler) {
        return session;
    }

    if (controls->data.running && !Hooks::SendToggleRunEvent(RE::INPUT_DEVICE::kKeyboard)) return session;
    session.test.walking_before = !controls->data.running;
    if (!session.test.walking_before) return session;

    const auto auto_move = Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::GetSingleton()->autoMove);
    if (!auto_move) return session;
    controls->autoMoveHandler->ProcessButton(auto_move, &controls->data);
    session.test.auto_move_enabled_by_test = controls->data.autoMove;
    if (session.test.auto_move_enabled_by_test) session.initial_position = player->GetPosition();
    return session;
}

void StopOwnedAutoMove(WalkSprintRegressionResult& test, RE::PlayerControls* controls) {
    if (!test.auto_move_enabled_by_test) return;
    test.cleanup_attempted = true;
    if (controls && controls->data.autoMove && controls->autoMoveHandler) {
        if (const auto auto_move = Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::GetSingleton()->autoMove)) {
            controls->autoMoveHandler->ProcessButton(auto_move, &controls->data);
        }
    }
    test.auto_move_after_cleanup = !controls || controls->data.autoMove;
    test.passed = test.passed && !test.auto_move_after_cleanup;
}

WalkSprintRegressionResult DispatchInitialSprint() {
    WalkSprintRegressionResult result;
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto dispatcher = DevBenchInput::GetDispatcher();
    if (!player || !controls) return result;
    result.player_loaded = true;

    const auto location = player->GetCurrentLocation();
    const auto cell = player->GetParentCell();
    result.riverwood_exterior = location && cell && !cell->IsInteriorCell() &&
        location->GetFormID() == 0x00013163;
    result.not_sneaking = !player->IsSneaking();
    result.moving = player->IsMoving();
    result.dispatcher_captured = dispatcher != nullptr;
    if (!result.riverwood_exterior || !result.not_sneaking || !result.moving || !result.dispatcher_captured) return result;

    result.walking_before = !controls->data.running;
    if (!result.walking_before && !Hooks::SendToggleRunEvent(RE::INPUT_DEVICE::kKeyboard)) return result;
    result.walking_before = !controls->data.running;
    if (!result.walking_before) return result;

    const auto sprint = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kKeyboard);
    if (!sprint) return result;
    RE::InputEvent* events[] = { sprint };
    result.sprint_dispatched = true;
    Hooks::InputHook::thunk(dispatcher, events);
    result.walking_after = !controls->data.running;
    result.passed = !result.walking_after;
    return result;
}

RegressionSession StartSprintRegression(RegressionSession session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    if (player && controls && session.test.auto_move_enabled_by_test) {
        const auto position = player->GetPosition();
        const auto dx = position.x - session.initial_position.x;
        const auto dy = position.y - session.initial_position.y;
        const auto dz = position.z - session.initial_position.z;
        const auto displaced = dx * dx + dy * dy + dz * dz >= kMinimumMovementDistanceSquared;
        session.test.moving = player->IsMoving();
        session.test.movement_observed = session.test.moving && displaced;
        if (session.test.movement_observed) {
            const auto setup = session.test;
            session.test = DispatchInitialSprint();
            session.test.menus_clear = setup.menus_clear;
            session.test.auto_move_was_active = setup.auto_move_was_active;
            session.test.auto_move_enabled_by_test = setup.auto_move_enabled_by_test;
            session.test.movement_observed = true;
        }
    }
    return session;
}

RegressionSession HoldSprint(RegressionSession session) {
    const auto dispatcher = DevBenchInput::GetDispatcher();
    if (!session.test.sprint_dispatched || !dispatcher) return session;
    const auto held_seconds = std::chrono::duration<float>(kSprintHeldTime).count();
    if (const auto sprint = Hooks::CreateButtonEvent(
            RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::GetSingleton()->sprint, 1.0F, held_seconds)) {
        RE::InputEvent* events[] = { sprint };
        Hooks::InputHook::thunk(dispatcher, events);
    }
    return session;
}

WalkSprintRegressionResult FinishRegression(RegressionSession session) {
    const auto player = RE::PlayerCharacter::GetSingleton();
    const auto controls = RE::PlayerControls::GetSingleton();
    const auto dispatcher = DevBenchInput::GetDispatcher();
    session.test.sprinting_after = player && player->AsActorState()->IsSprinting();
    if (session.test.sprint_dispatched && dispatcher) {
        const auto held_seconds = std::chrono::duration<float>(kSprintHeldTime).count();
        if (const auto sprint_up = Hooks::CreateButtonEvent(
                RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::GetSingleton()->sprint, 0.0F, held_seconds)) {
            RE::InputEvent* events[] = { sprint_up };
            Hooks::InputHook::thunk(dispatcher, events);
            session.test.sprint_up_dispatched = true;
        }
    }
    session.test.passed = session.test.movement_observed && session.test.walking_before &&
        !session.test.walking_after && session.test.sprinting_after && session.test.sprint_up_dispatched;
    StopOwnedAutoMove(session.test, controls);
    return session.test;
}

std::string Serialize(const WalkSprintRegressionResult& test) {
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    return std::string{"{\"ok\":"} + flag(test.passed) +
        ",\"playerLoaded\":" + flag(test.player_loaded) +
        ",\"riverwoodExterior\":" + flag(test.riverwood_exterior) +
        ",\"notSneaking\":" + flag(test.not_sneaking) +
        ",\"menusClear\":" + flag(test.menus_clear) +
        ",\"autoMoveWasActive\":" + flag(test.auto_move_was_active) +
        ",\"autoMoveEnabledByTest\":" + flag(test.auto_move_enabled_by_test) +
        ",\"movementObserved\":" + flag(test.movement_observed) +
        ",\"moving\":" + flag(test.moving) +
        ",\"dispatcherCaptured\":" + flag(test.dispatcher_captured) +
        ",\"walkingBefore\":" + flag(test.walking_before) +
        ",\"sprintDispatched\":" + flag(test.sprint_dispatched) +
        ",\"walkingAfter\":" + flag(test.walking_after) +
        ",\"sprintingAfter\":" + flag(test.sprinting_after) +
        ",\"sprintUpDispatched\":" + flag(test.sprint_up_dispatched) +
        ",\"cleanupAttempted\":" + flag(test.cleanup_attempted) +
        ",\"autoMoveAfterCleanup\":" + flag(test.auto_move_after_cleanup) + "}";
}

void RunWalkSprintRegression(void*, const char*, void* sink, DevBenchAPI::WriteFn write) {
    const auto begin_completion = std::make_shared<std::promise<RegressionSession>>();
    auto begin_result = begin_completion->get_future();
    SKSE::GetTaskInterface()->AddTask([begin_completion] { begin_completion->set_value(BeginRegression()); });
    if (begin_result.wait_for(kMainThreadTimeout) != std::future_status::ready) {
        write(sink, R"({"ok":false,"error":"game thread did not begin the walk/sprint test"})");
        return;
    }

    auto session = begin_result.get();
    if (!session.test.auto_move_enabled_by_test) {
        const auto json = Serialize(session.test);
        write(sink, json.c_str());
        return;
    }

    std::this_thread::sleep_for(kMovementSettleTime);
    const auto start_completion = std::make_shared<std::promise<RegressionSession>>();
    auto start_result = start_completion->get_future();
    SKSE::GetTaskInterface()->AddTask([start_completion, session] { start_completion->set_value(StartSprintRegression(session)); });
    if (start_result.wait_for(kMainThreadTimeout) != std::future_status::ready) {
        write(sink, R"({"ok":false,"error":"game thread did not start the sprint test"})");
        return;
    }

    session = start_result.get();
    std::this_thread::sleep_for(kSprintHeldTime);
    const auto hold_completion = std::make_shared<std::promise<RegressionSession>>();
    auto hold_result = hold_completion->get_future();
    SKSE::GetTaskInterface()->AddTask([hold_completion, session] { hold_completion->set_value(HoldSprint(session)); });
    if (hold_result.wait_for(kMainThreadTimeout) != std::future_status::ready) {
        write(sink, R"({"ok":false,"error":"game thread did not hold the sprint input"})");
        return;
    }

    session = hold_result.get();
    for (std::size_t frame = 0; frame < kSprintFrameCount; ++frame) {
        std::this_thread::sleep_for(kSprintFrameTime);
        const auto frame_completion = std::make_shared<std::promise<RegressionSession>>();
        auto frame_result = frame_completion->get_future();
        SKSE::GetTaskInterface()->AddTask([frame_completion, session] { frame_completion->set_value(HoldSprint(session)); });
        if (frame_result.wait_for(kMainThreadTimeout) != std::future_status::ready) {
            write(sink, R"({"ok":false,"error":"game thread stopped pumping held sprint input"})");
            return;
        }
        session = frame_result.get();
    }
    const auto finish_completion = std::make_shared<std::promise<WalkSprintRegressionResult>>();
    auto finish_result = finish_completion->get_future();
    SKSE::GetTaskInterface()->AddTask([finish_completion, session] { finish_completion->set_value(FinishRegression(session)); });
    if (finish_result.wait_for(kMainThreadTimeout) != std::future_status::ready) {
        SKSE::GetTaskInterface()->AddTask([] {
            if (const auto controls = RE::PlayerControls::GetSingleton(); controls && controls->data.autoMove && controls->autoMoveHandler) {
                if (const auto auto_move = Hooks::CreateButtonEvent(RE::INPUT_DEVICE::kKeyboard, RE::UserEvents::GetSingleton()->autoMove)) {
                    controls->autoMoveHandler->ProcessButton(auto_move, &controls->data);
                }
            }
        });
        write(sink, R"({"ok":false,"error":"game thread did not finish the walk/sprint test; cleanup queued"})");
        return;
    }

    const auto json = Serialize(finish_result.get());
    write(sink, json.c_str());
}
}

void DevBenchBridge::Register() {
    const auto api = DevBenchAPI::GetDevBenchInterface001();
    if (!api) { logger::info("DevBench is not installed; test tool not registered"); return; }
    constexpr auto descriptor = R"({"description":"Runs the SprintCancelsSneak walk/sprint regression through the mod input detour. It temporarily enables and then cleans up Skyrim Auto-Move in Riverwood exterior.","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"readOnly":false})";
    api->RegisterTool("sprint_cancels_sneak.walk_sprint_regression", descriptor, RunWalkSprintRegression, nullptr);
    logger::info("Registered DevBench walk/sprint regression tool");
}
