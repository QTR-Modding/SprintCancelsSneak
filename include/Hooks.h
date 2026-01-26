#pragma once

namespace Hooks {
    void Install();

    inline std::map<RE::INPUT_DEVICE, RE::ButtonEvent*> sneak_events;
    inline std::map<RE::INPUT_DEVICE, RE::ButtonEvent*> sprint_events;

    constexpr auto sprint_held_threshold_s = 250.f / 1000.0f;

    RE::ButtonEvent* CreateButtonEvent(RE::INPUT_DEVICE a_device, const RE::BSFixedString& user_event);
    RE::ButtonEvent* CreateSneakEvent(RE::INPUT_DEVICE a_device);
    RE::ButtonEvent* CreateSprintEvent(RE::INPUT_DEVICE a_device);
    void UpdateSneakSprintEvents();

    class ControlsChangedHook : public RE::Journal_SystemTab {
        using ProcessMessage_t = RE::BSEventNotifyControl(Journal_SystemTab::*)(
            const RE::BSGamerProfileEvent*, RE::BSTEventSource<RE::BSGamerProfileEvent>*);
        static inline REL::Relocation<ProcessMessage_t> ProcessEvent;
        RE::BSEventNotifyControl ProcessEvent_Hook(const RE::BSGamerProfileEvent* a_event,
                                                   RE::BSTEventSource<RE::BSGamerProfileEvent>* a_eventSource);

    public:
        static void InstallHook(const REL::VariantID& varID);
    };

    struct InputHook {
        static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_event);
        static inline REL::Relocation<decltype(thunk)> func;
        static bool ProcessInput(RE::InputEvent* event);
        static void InstallHook(SKSE::Trampoline& a_trampoline);
        static void GetUp(const RE::InputEvent* event);
    };
};