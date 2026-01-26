#include "Hooks.h"
#include "Settings.h"

RE::ButtonEvent* Hooks::CreateButtonEvent(RE::INPUT_DEVICE a_device, const RE::BSFixedString& user_event) {
    const auto control_map = RE::ControlMap::GetSingleton();
    const auto key = control_map->GetMappedKey(user_event, a_device);
    const auto button_event = RE::ButtonEvent::Create(a_device, user_event, key, 1.f, 0.f);
    if (!button_event) logger::error("Failed to create button_event");
    return button_event;
}

RE::ButtonEvent* Hooks::CreateSneakEvent(const RE::INPUT_DEVICE a_device) {
    return CreateButtonEvent(a_device, RE::UserEvents::GetSingleton()->sneak);
}

RE::ButtonEvent* Hooks::CreateSprintEvent(RE::INPUT_DEVICE a_device) {
    return CreateButtonEvent(a_device, RE::UserEvents::GetSingleton()->sprint);
}

void Hooks::UpdateSneakSprintEvents() {
    const auto control_map = RE::ControlMap::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    for (auto& [device, event] : sprint_events) {
        event->SetIDCode(control_map->GetMappedKey(user_events->sprint, device));
    }
    for (auto& [device, event] : sneak_events) {
        event->SetIDCode(control_map->GetMappedKey(user_events->sneak, device));
    }
}

void Hooks::Install() {
    ControlsChangedHook::InstallHook(RE::VTABLE_Journal_SystemTab[1]);

    auto& trampoline = SKSE::GetTrampoline();
    constexpr size_t size_per_hook = 14;
    SKSE::AllocTrampoline(1 * size_per_hook);
    InputHook::InstallHook(trampoline);
}

RE::BSEventNotifyControl Hooks::ControlsChangedHook::ProcessEvent_Hook(const RE::BSGamerProfileEvent* a_event,
                                                                       RE::BSTEventSource<RE::BSGamerProfileEvent>*
                                                                       a_eventSource) {
    UpdateSneakSprintEvents();
    return ProcessEvent(this, a_event, a_eventSource);
}

void Hooks::ControlsChangedHook::InstallHook(const REL::VariantID& varID) {
    REL::Relocation vTable(varID);
    ProcessEvent = vTable.write_vfunc(0x1, &ControlsChangedHook::ProcessEvent_Hook);
}

void Hooks::InputHook::thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_event) {
    if (!a_dispatcher || !a_event) {
        return func(a_dispatcher, a_event);
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player->IsMoving() || !player->IsSneaking()) {
        return func(a_dispatcher, a_event);
    }

    auto first = *a_event;
    auto last = *a_event;
    size_t length = 0;

    for (auto current = *a_event; current; current = current->next) {
        if (ProcessInput(current)) {
            if (current != last) {
                last->next = current->next;
            } else {
                last = current->next;
                first = current->next;
            }
        } else {
            last = current;
            ++length;
        }
    }

    if (length == 0) {
        constexpr RE::InputEvent* const dummy[] = {nullptr};
        func(a_dispatcher, dummy);
    } else {
        RE::InputEvent* const e[] = {first};
        func(a_dispatcher, e);
    }
}

bool Hooks::InputHook::ProcessInput(RE::InputEvent* event) {
    bool block = false;
    if (auto button_event = event->AsButtonEvent()) {
        if (button_event->GetUserEvent() == RE::UserEvents::GetSingleton()->sprint) {
            block = true;
            if (!button_event->IsUp()) {
                if (button_event->HeldDuration() >= sprint_held_threshold_s) {
                    GetUp(event);
                }
            } else if (!RE::PlayerCharacter::GetSingleton()->AsActorState()->IsSprinting()) {
                if (const auto device = event->GetDevice(); sprint_events.contains(device)) {
                    const auto player_controls = RE::PlayerControls::GetSingleton();
                    const auto sprint_event = sprint_events.at(device);
                    player_controls->sprintHandler->ProcessButton(sprint_event, &player_controls->data);
                    SKSE::GetTaskInterface()->AddTask([button_event]() {
                        const auto player_controls2 = RE::PlayerControls::GetSingleton();
                        player_controls2->sprintHandler->ProcessButton(button_event, &player_controls2->data);
                    });
                }
            }
        }
    }
    return block;
}

void Hooks::InputHook::InstallHook(SKSE::Trampoline& a_trampoline) {
    const REL::Relocation<std::uintptr_t> target{REL::RelocationID(67315, 68617)};
    func = a_trampoline.write_call<5>(target.address() + 0x7B, thunk);
}

void Hooks::InputHook::GetUp(const RE::InputEvent* event) {
    if (ModCompatibility::TUDM::is_installed) {
        ModCompatibility::TUDM::StopSneak();
        return;
    }
    if (const auto device = event->GetDevice(); sneak_events.contains(device)) {
        const auto player_controls = RE::PlayerControls::GetSingleton();
        const auto sneak_event = sneak_events.at(device);
        player_controls->sneakHandler->ProcessButton(sneak_event, &player_controls->data);
        if (sprint_events.contains(device)) {
            player_controls->sprintHandler->ProcessButton(sprint_events.at(device), &player_controls->data);
        }
    }
}