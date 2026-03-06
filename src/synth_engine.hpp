#pragma once
#include "voice.hpp"
#include "sample_db.hpp"
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <vector>
#include <cstdint>

// ペダルクリック（機械的ノイズ — Pedal Up 用）
struct PedalClick {
    enum class Phase { IDLE, ATTACK, RELEASE };

    Phase    phase = Phase::IDLE;
    float    env = 0.0f;
    float    attack_rate = 0.0f;
    float    release_rate = 0.0f;
    float    lpf_state = 0.0f;
    float    hpf_state = 0.0f;
    float    hpf_prev  = 0.0f;
    float    lp_coeff = 0.0f;
    float    hp_coeff = 0.0f;
    float    volume = 0.0f;
    uint32_t rng = 48271u;

    float    attack_ms  = 2.0f;
    float    release_ms = 40.0f;

    float next_noise() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(rng)) * (1.0f / 2147483648.0f);
    }

    void trigger(int sr);
    void mix(float* buf, int frames);
};

// 弦共鳴（ダンパーリフト時のシンパシー共鳴 — Pedal Down 用）
// 現在発音中のノートの周波数 + 倍音で共鳴
struct PedalResonance {
    enum class Phase { IDLE, ATTACK, SUSTAIN, RELEASE };
    static constexpr int MAX_PARTIALS = 24;  // ノート×倍音

    Phase   phase = Phase::IDLE;
    float   env = 0.0f;
    float   attack_rate = 0.0f;
    float   release_rate = 0.0f;
    int     num_partials = 0;
    double  phases[MAX_PARTIALS] = {};
    double  incs[MAX_PARTIALS]   = {};
    float   amps[MAX_PARTIALS]   = {};
    float   volume = 0.0f;

    float   attack_ms  = 80.0f;   // CC83 で調整 (0〜200ms)
    float   release_ms = 400.0f;  // CC84 で調整 (100〜800ms)

    // active_notes: 現在鳴っているMIDIノート番号の配列
    void trigger_down(int sr, const int* active_notes, int num_notes);
    void trigger_up();
    void mix(float* buf, int frames);
};

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
    float     volume_     = 1.0f;       // CC7: Volume (0〜1)
    float     expression_ = 1.0f;       // CC11: Expression (0〜1)

    std::array<Voice, MAX_VOICES> voices_;
    SpscQueue<MidiEvent, 256>     event_queue_;
    PedalClick     pedal_click_;       // Pedal Up 機械的クリック
    PedalResonance pedal_resonance_;   // Pedal Down 弦共鳴
    float resonance_vol_ = 0.15f;     // CC82: 共鳴音量
    float click_vol_     = 0.15f;     // CC85: クリック音量
};
