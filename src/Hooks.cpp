#include "Hooks.h"
#include "Settings.h"

RE::ButtonEvent* Hooks::CreateButtonEvent(const RE::INPUT_DEVICE a_device, const RE::BSFixedString& user_event, float a_val, float a_helddownsecs) {
    const auto control_map = RE::ControlMap::GetSingleton();
    const auto key = control_map->GetMappedKey(user_event, a_device);
    const auto button_event = RE::ButtonEvent::Create(a_device, user_event, key, a_val, a_helddownsecs);
    if (!button_event) logger::error("Failed to create button_event");
    return button_event;
}

RE::ButtonEvent* Hooks::CreateSneakEvent(const RE::INPUT_DEVICE a_device) {
    const auto a_event = CreateButtonEvent(a_device, RE::UserEvents::GetSingleton()->sneak);
    return a_event;
}

RE::ButtonEvent* Hooks::CreateSprintEvent(const RE::INPUT_DEVICE a_device) {
    const auto a_event = CreateButtonEvent(a_device, RE::UserEvents::GetSingleton()->sprint);
    return a_event;
}

RE::ButtonEvent* Hooks::CreateToggleRunEvent(const RE::INPUT_DEVICE a_device) {
    const auto a_event = CreateButtonEvent(a_device, RE::UserEvents::GetSingleton()->toggleRun);
    return a_event;
}

void Hooks::UpdateButtonEvents() {
    const auto control_map = RE::ControlMap::GetSingleton();
    const auto user_events = RE::UserEvents::GetSingleton();
    for (auto& [device, event] : sprint_events) {
        event->SetIDCode(control_map->GetMappedKey(user_events->sprint, device));
    }
    for (auto& [device, event] : sneak_events) {
        event->SetIDCode(control_map->GetMappedKey(user_events->sneak, device));
    }
    for (auto& [device, event] : toggle_run_events) {
        event->SetIDCode(control_map->GetMappedKey(user_events->toggleRun, device));
    }
}

bool Hooks::SendButtonEvent(RE::ButtonEvent* a_button, RE::PlayerInputHandler* a_handler) {
    if (a_button && a_handler) {
        a_handler->ProcessButton(a_button, &RE::PlayerControls::GetSingleton()->data);
        return true;
    }
    return false;
}

bool Hooks::SendSneakEvent(const RE::INPUT_DEVICE a_device) {
    if (const auto it = sneak_events.find(a_device); it != sneak_events.end()) {
        return SendButtonEvent(it->second, RE::PlayerControls::GetSingleton()->sneakHandler);
    }
    return false;
}

bool Hooks::SendSprintEvent(const RE::INPUT_DEVICE a_device) {
    if (const auto it = sprint_events.find(a_device); it != sprint_events.end()) {
        return SendButtonEvent(it->second, RE::PlayerControls::GetSingleton()->sprintHandler);
    }
    return false;
}

bool Hooks::SendToggleRunEvent(const RE::INPUT_DEVICE a_device) {
    if (const auto it = toggle_run_events.find(a_device); it != toggle_run_events.end()) {
        return SendButtonEvent(it->second, RE::PlayerControls::GetSingleton()->toggleRunHandler);
    }
    return false;
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
    UpdateButtonEvents();
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

    const auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->IsMoving()) {
        return func(a_dispatcher, a_event);
    }

    if (!player->IsSneaking()) {
        for (auto current = *a_event; current; current = current->next) {
            if (const auto button_event = current->AsButtonEvent();
                button_event && button_event->GetUserEvent() == RE::UserEvents::GetSingleton()->sprint &&
                !button_event->IsUp()) {
                ForceRun(current);
                break;
            }
        }
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
    static bool push_exit_sneak = false;
    bool block = false;
    if (auto button_event = event->AsButtonEvent()) {
        if (button_event->GetUserEvent() == RE::UserEvents::GetSingleton()->sprint) {
            block = true;
            if (!button_event->IsUp()) {
                push_exit_sneak |= ForceRun(event);
                if (button_event->HeldDuration() > sprint_held_threshold_s + 0.125f * push_exit_sneak) {
                    push_exit_sneak = false;
                    GetUp(event);
                }
            } else if (!RE::PlayerCharacter::GetSingleton()->AsActorState()->IsSprinting()) {
                // for sneak roll
                if (const auto device = event->GetDevice(); SendSprintEvent(device)) {
                    SKSE::GetTaskInterface()->AddTask([button_event]() {
                        SendButtonEvent(button_event, RE::PlayerControls::GetSingleton()->sprintHandler);
                    });
                }
            }
        }
    }
    return block;
}

void Hooks::InputHook::InstallHook(SKSE::Trampoline& a_trampoline) {
    const REL::Relocation target{REL::RelocationID(67315, 68617)};
    func = a_trampoline.write_call<5>(target.address() + 0x7B, thunk);
}

void Hooks::InputHook::GetUp(const RE::InputEvent* event) {
    if (ModCompatibility::TUDM::is_installed) {
        ModCompatibility::TUDM::StopSneak();
        return;
    }
    if (const auto device = event->GetDevice(); SendSneakEvent(device)) {
        SendSprintEvent(device);
    }
}

bool Hooks::InputHook::ForceRun(const RE::InputEvent* event) {
    if (!RE::PlayerControls::GetSingleton()->data.running) {
        return SendToggleRunEvent(event->GetDevice());
    }
    return false;
}