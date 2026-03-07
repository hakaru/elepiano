#pragma once
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <cstdint>
#include <memory>

// setBfree Hammond B3 シンセ — C++ ラッパー
// vendor/setBfree を静的リンクして直接呼び出す
class SetBfreeEngine {
public:
    explicit SetBfreeEngine(int sample_rate);
    ~SetBfreeEngine();

    SetBfreeEngine(const SetBfreeEngine&) = delete;
    SetBfreeEngine& operator=(const SetBfreeEngine&) = delete;

    void push_event(const MidiEvent& ev);
    void mix(float* buf, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
