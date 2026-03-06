#pragma once
#include <cstring>
#include "spsc_queue.hpp"

// Rhodes FX チェーン: トレモロ → プリアンプ → EQ → コーラス → テープディレイ
// ステレオ float バッファ（インターリーブ L,R,L,R...）をインプレース処理
class FxChain {
public:
    explicit FxChain(int sample_rate);

    void process(float* buf, int frames);   // オーディオスレッドから呼ぶ
    void set_param(int cc, int value);      // MIDIスレッドから呼ぶ（ロックフリー push）

private:
    int sr_;

    // ── MIDI→オーディオ スレッド間パラメータ転送 ──
    struct FxEvent { int cc; int value; };
    SpscQueue<FxEvent, 32> param_queue_;
    void apply_param(int cc, int value);    // オーディオスレッド内でパラメータ適用

    // ── トレモロ（AM変調, L/R 90° 位相差） ──
    struct {
        float depth = 0.0f;     // 0.0〜0.8
        double phase = 0.0;
        double inc   = 0.0;     // rate Hz → phase/sample
    } trem_;
    void process_tremolo(float* buf, int frames);

    // ── オーバードライブ（非対称クリッピング + トーンフィルタ） ──
    struct {
        float drive = 1.0f;     // 1.0〜8.0
        float tone  = 0.5f;     // 0.0(dark)〜1.0(bright)
        float lpf_l = 0.0f, lpf_r = 0.0f;  // ポストドライブ LPF 状態
    } preamp_;
    void process_preamp(float* buf, int frames);

    // ── EQ（2バンド シェルビング, Direct Form II Transposed） ──
    struct Biquad {
        float b0=1, b1=0, b2=0, a1=0, a2=0;
        float s1[2]={}, s2[2]={};  // [0]=L, [1]=R
    };
    Biquad eq_lo_, eq_hi_;
    float eq_lo_db_ = 0.0f, eq_hi_db_ = 0.0f;
    void compute_shelf(Biquad& bq, float fc, float gain_db, bool high);
    void process_biquad(Biquad& bq, float* buf, int frames);

    // ── コーラス（LFO 変調ディレイ） ──
    static constexpr int CHORUS_BUF = 2048;  // 2のべき乗
    struct {
        float buf_l[CHORUS_BUF]={}, buf_r[CHORUS_BUF]={};
        int   write = 0;
        double phase_l = 0.0, phase_r = 0.5;  // 180° 位相差
        double inc = 0.0;
        float depth_ms = 8.0f;
        float wet = 0.0f;
    } chorus_;
    void process_chorus(float* buf, int frames);

    // ── テープエコー（サチュレーション + ウォーム LPF + Wet/Dry） ──
    static constexpr int DELAY_BUF = 32768;  // 2のべき乗（ビットマスク高速化）
    struct {
        float buf_l[DELAY_BUF]={}, buf_r[DELAY_BUF]={};
        int   write = 0;
        float delay_samples = 0.0f;
        float feedback = 0.4f;
        float lpf_l = 0.0f, lpf_r = 0.0f;  // 1次 IIR 状態
        float wet = 0.5f;                    // ウェットレベル
        float wet_target = 0.5f;             // CC80 で設定
    } delay_;
    void process_delay(float* buf, int frames);

    // sin LUT
    static constexpr int LUT_SIZE = 256;
    float sin_lut_[LUT_SIZE];
    float lut(double phase) const {
        return sin_lut_[static_cast<int>(phase * LUT_SIZE) & (LUT_SIZE - 1)];
    }
};
