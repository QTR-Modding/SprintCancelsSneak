#pragma once

#include <memory>

namespace DevBenchInput {
    class Frame {
    public:
        Frame(
            RE::BSTEventSource<RE::InputEvent*>* a_dispatcher,
            RE::InputEvent* const*& a_events);
        ~Frame();

        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        Frame(Frame&&) = delete;
        Frame& operator=(Frame&&) = delete;

    private:
        struct State;
        std::unique_ptr<State> state_;
    };

    RE::BSTEventSource<RE::InputEvent*>* GetDispatcher();
}
