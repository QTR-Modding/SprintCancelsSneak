#pragma once

#include <memory>

namespace DevBenchSneakMatrix {
    struct NativeInputSession;

    struct NativeInputFrame {
        RE::InputEvent* head = nullptr;
        RE::InputEvent* original_head = nullptr;
        std::shared_ptr<NativeInputSession> session;
        bool injected = false;
        bool release_frame = false;
        bool forward_down = false;
        bool forward_held = false;
        bool sprint_down = false;
        bool sprint_held = false;
    };

    NativeInputFrame BeginNativeInputFrame(RE::InputEvent* original_head);
    void EndNativeInputFrame(NativeInputFrame& frame);
    void Register();
}