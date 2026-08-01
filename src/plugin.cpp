#include "Hooks.h"
#include "logger.h"
#include "Settings.h"

#ifdef SCS_WITH_DEVBENCH
#include "DevBenchIntegration.h"
#endif

namespace {
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnMessage(SKSE::MessagingInterface::Message* message) {
#ifdef SCS_WITH_DEVBENCH
        if (message->type == SKSE::MessagingInterface::kPostLoad) {
            DevBenchIntegration::Register();
        }
#endif
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::sneak_events[RE::INPUT_DEVICE::kKeyboard] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kKeyboard);
            Hooks::sneak_events[RE::INPUT_DEVICE::kMouse] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kMouse);
            Hooks::sneak_events[RE::INPUT_DEVICE::kGamepad] = Hooks::CreateSneakEvent(RE::INPUT_DEVICE::kGamepad);

            Hooks::sprint_events[RE::INPUT_DEVICE::kKeyboard] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kKeyboard);
            Hooks::sprint_events[RE::INPUT_DEVICE::kMouse] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kMouse);
            Hooks::sprint_events[RE::INPUT_DEVICE::kGamepad] = Hooks::CreateSprintEvent(RE::INPUT_DEVICE::kGamepad);

            Hooks::toggle_run_events[RE::INPUT_DEVICE::kKeyboard] = Hooks::CreateToggleRunEvent(RE::INPUT_DEVICE::kKeyboard);
            Hooks::toggle_run_events[RE::INPUT_DEVICE::kMouse] = Hooks::CreateToggleRunEvent(RE::INPUT_DEVICE::kMouse);
            Hooks::toggle_run_events[RE::INPUT_DEVICE::kGamepad] = Hooks::CreateToggleRunEvent(RE::INPUT_DEVICE::kGamepad);

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