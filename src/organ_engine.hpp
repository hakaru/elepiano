#pragma once
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <cstdint>

/// Hammond B3 スタイルのトーンホイールオルガンエンジン
///
/// 合成方式: 9 ドローバー × 最大 128 音 加算合成
///   - sin LUT (4096 エントリ、float)
///   - ドローバー 0-8 の振幅スケール
///   - Leslie シミュレーション: AM + ステレオパン変調
///   - tanh ソフトクリップ
///
/// MIDI CC マッピング:
///   CC 64 (Sustain Pedal): val >= 64 → Leslie Fast (6.7Hz), < 64 → Slow (0.7Hz)
///   CC 12-20: ドローバー 0-8 (val 0-127 → drawbar 0-8)
class OrganEngine {
public:
    static constexpr int NUM_DRAWBARS = 9;
    static constexpr int MAX_NOTES    = 128;
    static constexpr int LUT_SIZE     = 4096;

    // ドローバー番号に対するセミトーンオフセット (8' = 0)
    // 16'  5⅓'  8'  4'  2⅔'  2'  1⅗'  1⅓'  1'
    static constexpr int DRAWBAR_SEMITONES[NUM_DRAWBARS] = {
        -12,  7,  0,  12,  19,  24,  28,  31,  36
    };

    explicit OrganEngine(int sample_rate = 44100);

    // MIDI スレッドから呼ぶ（ロックフリー push）
    void push_event(const MidiEvent& ev);

    // オーディオスレッドから呼ぶ
    // buf: ステレオ float (frames * 2 要素)
    void mix(float* buf, int frames);

    // ドローバー値設定 (0-8)
    void set_drawbar(int idx, int value);

private:
    void _note_on(int note, int velocity);
    void _note_off(int note);
    void _handle_cc(int cc_num, int cc_val);

    struct NoteState {
        bool   active           = false;
        double phase[NUM_DRAWBARS] = {};
        double inc[NUM_DRAWBARS]   = {};
        float  gain                = 0.0f;
    };

    int       sample_rate_;
    int       drawbars_[NUM_DRAWBARS];   // 0-8
    NoteState notes_[MAX_NOTES];
    float     sin_lut_[LUT_SIZE];

    // Leslie
    double leslie_phase_     = 0.0;
    double leslie_phase_inc_;             // samples/cycle
    bool   leslie_fast_      = false;

    static constexpr double LESLIE_FAST_HZ = 6.7;
    static constexpr double LESLIE_SLOW_HZ = 0.7;
    static constexpr float  MASTER_GAIN    = 0.3f;

    SpscQueue<MidiEvent, 64> event_queue_;
};
