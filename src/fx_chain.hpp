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

    // ── オーバードライブ（2段カスケード + 2xオーバーサンプリング + キャビネット） ──
    struct {
        float drive = 1.0f;     // 1.0〜8.0
        float tone  = 0.5f;     // 0.0(dark)〜1.0(bright)
        float lpf_l = 0.0f, lpf_r = 0.0f;  // ポストドライブ LPF 状態
        // 2x オーバーサンプリング用 halfband フィルタ状態
        float up_prev_l = 0.0f, up_prev_r = 0.0f;   // アップサンプル用
        float dn_prev_l = 0.0f, dn_prev_r = 0.0f;   // ダウンサンプル用
        float dn_acc_l = 0.0f, dn_acc_r = 0.0f;
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

    // キャビネットシミュレーション（Suitcase スピーカー風 BPF）
    Biquad cab_lo_, cab_hi_;  // HPF 200Hz + LPF 5kHz
    void process_cabinet(float* buf, int frames);

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

    // ── Space エフェクト（Tape Echo / Room Reverb / Plate Reverb 切替） ──
    enum class SpaceMode { TAPE_ECHO, ROOM, PLATE };
    SpaceMode space_mode_ = SpaceMode::TAPE_ECHO;
    float space_wet_ = 0.5f;
    float space_wet_target_ = 0.5f;
    void process_space(float* buf, int frames);

    // テープエコー
    static constexpr int DELAY_BUF = 32768;
    struct {
        float buf_l[DELAY_BUF]={}, buf_r[DELAY_BUF]={};
        int   write = 0;
        float delay_samples = 0.0f;
        float feedback = 0.4f;
        float lpf_l = 0.0f, lpf_r = 0.0f;
    } delay_;
    void process_tape_echo(float* buf, int frames);

    // Room Reverb (Schroeder: 4 comb + 2 allpass)
    static constexpr int COMB_BUF = 8192;
    static constexpr int AP_BUF   = 2048;
    struct CombFilter {
        float buf[COMB_BUF]={};
        int   write = 0;
        int   delay = 1000;
        float feedback = 0.7f;
        float lpf = 0.0f;
    };
    struct AllpassFilter {
        float buf[AP_BUF]={};
        int   write = 0;
        int   delay = 500;
        float feedback = 0.5f;
    };
    CombFilter comb_l_[4], comb_r_[4];
    AllpassFilter ap_l_[2], ap_r_[2];
    float room_decay_ = 0.7f;
    float room_size_  = 0.5f;
    void setup_room(float size, float decay);
    void process_room(float* buf, int frames);

    // Plate Reverb (6 allpass chain)
    static constexpr int PLATE_AP_BUF = 4096;
    struct PlateAP {
        float buf[PLATE_AP_BUF]={};
        int   write = 0;
        int   delay = 300;
        float feedback = 0.5f;
    };
    PlateAP plate_l_[6], plate_r_[6];
    float plate_decay_ = 0.7f;
    void setup_plate(float decay);
    void process_plate(float* buf, int frames);

    // sin LUT
    static constexpr int LUT_SIZE = 256;
    float sin_lut_[LUT_SIZE];
    float lut(double phase) const {
        return sin_lut_[static_cast<int>(phase * LUT_SIZE) & (LUT_SIZE - 1)];
    }
};
