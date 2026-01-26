#include "Hooks.h"
#include "logger.h"
#include "Settings.h"

namespace {
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnMessage(SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::sneak_events[RE::INPUT_DEVICE::kKeyboard] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kKeyboard);
            Hooks::sneak_events[RE::INPUT_DEVICE::kMouse] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kMouse);
            Hooks::sneak_events[RE::INPUT_DEVICE::kGamepad] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kGamepad);

            Hooks::sprint_events[RE::INPUT_DEVICE::kKeyboard] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kKeyboard);
            Hooks::sprint_events[RE::INPUT_DEVICE::kMouse] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kMouse);
            Hooks::sprint_events[RE::INPUT_DEVICE::kGamepad] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kGamepad);

            ModCompatibility::TUDM::is_installed = ModCompatibility::TUDM::IsInstalled();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    Hooks::Install();
    return true;
}