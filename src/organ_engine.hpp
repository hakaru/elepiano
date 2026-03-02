#pragma once
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <cstdint>

/// Hammond B3 スタイルのトーンホイールオルガンエンジン（物理モデリング版）
///
/// MIDI チャンネル割り当て:
///   CH1 (0) → Upper Manual  (上鍵盤): ドローバー 9本
///   CH2 (1) → Lower Manual  (下鍵盤): ドローバー 9本
///   CH3 (2) → Pedal Board   (ペダル): ドローバー 9本 (デフォルト 16'+8')
///
/// MIDI CC マッピング（チャンネルで自動振り分け）:
///   CC  1  : V/C モード (0=off, 1-3=V1-V3 vibrato, 4-6=C1-C3 chorus)
///   CC  7  : 各マニュアルの音量 (0-127)
///   CC 12-20: ドローバー 0-8 (val 0-127 → drawbar 0-8) ← チャンネルで振り分け
///   CC 64  : Leslie Fast(6.7Hz) / Slow(0.7Hz) 切替 (どのチャンネルでも可)
///            ※ 即時切替ではなく指数収束でモーター慣性を再現
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
        float  click_env           = 0.0f;  // キーオンクリック エンベロープ
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

    // ─── Leslie (全マニュアル共通) ───────────────────────────────────────────
    // モーター慣性を再現する指数収束モデル（実機 Leslie 44W 測定値）
    double leslie_phase_       = 0.0;
    double leslie_current_hz_;   // 現在の回転速度
    double leslie_target_hz_;    // 目標回転速度

    static constexpr double LESLIE_FAST_HZ        = 6.7;
    static constexpr double LESLIE_SLOW_HZ        = 0.7;
    static constexpr double LESLIE_HORN_ACCEL_TAU = 0.161;  // 加速時定数 [s]
    static constexpr double LESLIE_HORN_DECEL_TAU = 0.321;  // 減速時定数 [s]

    // ─── キーオンクリック ────────────────────────────────────────────────────
    // 鍵盤接点閉鎖時の電気過渡（6倍音 + 指数減衰 70ms）
    float  click_decay_;  // コンストラクタで計算: expf(-3/(0.07*sample_rate))

    static constexpr float CLICK_AMPLITUDE = 0.15f;
    static constexpr float CLICK_DECAY_MS  = 70.0f;

    // ─── Vibrato / Chorus スキャナー ────────────────────────────────────────
    // Hammond 内蔵スキャナービブラート（7Hz LFO + 遅延ライン）
    static constexpr int    VIB_BUF_SIZE = 128;  // 2のべき乗（マスク演算用）
    static constexpr double VIB_LFO_HZ   = 7.0;

    // V/C モード別最大遅延サンプル数 (44100Hz 基準)
    // index: 0=off, 1=V1, 2=V2, 3=V3, 4=C1, 5=C2, 6=C3
    static constexpr float VIB_DEPTHS[7] = { 0.0f, 22.0f, 44.0f, 56.0f,
                                              22.0f, 44.0f, 56.0f };

    float  vib_buf_[VIB_BUF_SIZE] = {};
    int    vib_write_pos_         = 0;
    double vib_lfo_phase_         = 0.0;
    double vib_lfo_inc_;   // コンストラクタで計算: VIB_LFO_HZ / sample_rate
    int    vib_mode_       = 5;  // デフォルト C2（コーラス中程度）

    // ─── トーンホイール磁束高調波 ────────────────────────────────────────────
    // 実機トーンホイールの非純正弦波成分（実測 2〜5%）
    static constexpr float TW_H2 = 0.03f;  // 2倍音 3%
    static constexpr float TW_H3 = 0.02f;  // 3倍音 2%

    static constexpr float MASTER_GAIN = 0.2f;  // 3 section 合算のため

    SpscQueue<MidiEvent, 64> event_queue_;
};
