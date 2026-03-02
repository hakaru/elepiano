#pragma once
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <cstdint>

/// Hammond B3 スタイルのトーンホイールオルガンエンジン
///
/// MIDI チャンネル割り当て:
///   CH1 (0) → Upper Manual  (上鍵盤): ドローバー 9本
///   CH2 (1) → Lower Manual  (下鍵盤): ドローバー 9本
///   CH3 (2) → Pedal Board   (ペダル): ドローバー 9本 (デフォルト 16'+8')
///
/// MIDI CC マッピング（チャンネルで自動振り分け）:
///   CC  7  : 各マニュアルの音量 (0-127)
///   CC 12-20: ドローバー 0-8 (val 0-127 → drawbar 0-8) ← チャンネルで振り分け
///   CC 64  : Leslie Fast(6.7Hz) / Slow(0.7Hz) 切替 (どのチャンネルでも可)
class OrganEngine {
public:
    static constexpr int NUM_DRAWBARS = 9;
    static constexpr int MAX_NOTES    = 128;
    static constexpr int LUT_SIZE     = 4096;
    static constexpr int NUM_SECTIONS = 3;   // Upper / Lower / Pedal

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

    // ドローバー値設定 (section: 0=Upper 1=Lower 2=Pedal, drawbar_idx: 0-8, value: 0-8)
    void set_drawbar(int section, int drawbar_idx, int value);

private:
    struct NoteState {
        bool   active              = false;
        double phase[NUM_DRAWBARS] = {};
        double inc[NUM_DRAWBARS]   = {};
        float  gain                = 0.0f;
    };

    struct ManualSection {
        int       ch           = 0;
        int       drawbars[NUM_DRAWBARS] = {};
        float     volume       = 1.0f;
        int       active_count = 0;   // アクティブノート数（128 全走査回避用）
        NoteState notes[MAX_NOTES];
    };

    void _note_on(int ch, int note, int velocity);
    void _note_off(int ch, int note);
    void _handle_cc(int ch, int cc_num, int cc_val);
    ManualSection* _section_for_ch(int ch);

    int           sample_rate_;
    ManualSection sections_[NUM_SECTIONS];  // [0]=Upper [1]=Lower [2]=Pedal
    float         sin_lut_[LUT_SIZE];

    // Leslie (全マニュアル共通)
    double leslie_phase_     = 0.0;
    double leslie_phase_inc_;
    bool   leslie_fast_      = false;

    static constexpr double LESLIE_FAST_HZ = 6.7;
    static constexpr double LESLIE_SLOW_HZ = 0.7;
    static constexpr float  MASTER_GAIN    = 0.2f;  // 3 section 合算のため 0.3 → 0.2

    SpscQueue<MidiEvent, 64> event_queue_;
};
