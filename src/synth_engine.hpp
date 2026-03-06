#pragma once
#include "voice.hpp"
#include "sample_db.hpp"
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <vector>
#include <cstdint>

class SynthEngine {
public:
    static constexpr int MAX_VOICES = 32;
    static constexpr int MAX_PROGRAMS = 16;

    // 音色ペア（attack + release）
    struct Program {
        SampleDB* attack  = nullptr;
        SampleDB* release = nullptr;
    };

    SynthEngine(int sample_rate = 44100);

    // 音色を登録（program 0〜15）
    void add_program(int program, SampleDB* attack, SampleDB* release = nullptr);

    // MIDIスレッドから呼ぶ（ロックフリー push）
    void push_event(const MidiEvent& ev);

    // オーディオスレッドから呼ぶ
    void mix(float* buf, int frames);

private:
    void _note_on(int midi_note, int velocity);
    void _note_off(int midi_note);
    void _cc(int cc_num, int cc_val);
    void _program_change(int program);
    void _start_release_voice(int midi_note, int velocity);
    int  oldest_voice_idx() const;

    std::array<Program, MAX_PROGRAMS> programs_;
    int       current_program_ = 0;
    SampleDB* db_         = nullptr;       // 現在の attack DB
    SampleDB* release_db_ = nullptr;       // 現在の release DB
    int       sample_rate_;
    uint64_t  sample_counter_ = 0;
    bool      sustain_held_   = false;
    float     release_time_s_ = 0.050f;  // CC74 で調整 (0ms〜200ms, default 50ms)

    std::array<Voice, MAX_VOICES> voices_;
    SpscQueue<MidiEvent, 256>     event_queue_;
};
