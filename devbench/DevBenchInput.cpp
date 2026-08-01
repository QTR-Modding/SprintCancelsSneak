#include "DevBenchInput.h"

#include "DevBenchSneakMatrix.h"

namespace {
    RE::BSTEventSource<RE::InputEvent*>* g_dispatcher = nullptr;
}

struct DevBenchInput::Frame::State {
    DevBenchSneakMatrix::NativeInputFrame native_frame;
    RE::InputEvent* injected_head = nullptr;
};

DevBenchInput::Frame::Frame(
    RE::BSTEventSource<RE::InputEvent*>* a_dispatcher,
    RE::InputEvent* const*& a_events) {
    g_dispatcher = a_dispatcher;
    if (!a_events) {
        return;
    }

    auto state = std::make_unique<State>();
    state->native_frame = DevBenchSneakMatrix::BeginNativeInputFrame(*a_events);
    if (state->native_frame.injected) {
        state->injected_head = state->native_frame.head;
        a_events = &state->injected_head;
    }
    state_ = std::move(state);
}

DevBenchInput::Frame::~Frame() {
    if (state_) {
        DevBenchSneakMatrix::EndNativeInputFrame(state_->native_frame);
    }
}

RE::BSTEventSource<RE::InputEvent*>* DevBenchInput::GetDispatcher() {
    return g_dispatcher;
}
