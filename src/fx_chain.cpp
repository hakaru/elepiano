#include "fx_chain.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

static constexpr double PI2 = 2.0 * M_PI;

FxChain::FxChain(int sample_rate) : sr_(sample_rate)
{
    for (int i = 0; i < LUT_SIZE; ++i)
        sin_lut_[i] = static_cast<float>(std::sin(PI2 * i / LUT_SIZE));

    // デフォルト LFO レート
    trem_.inc = 2.0 / sr_;   // 2 Hz
    chorus_.inc = 1.0 / sr_; // 1 Hz

    // EQ: フラット初期状態
    compute_shelf(eq_lo_, 150.0f, 0.0f, false);
    compute_shelf(eq_hi_, 3000.0f, 0.0f, true);

    // キャビネットシム: Suitcase スピーカー風 (HPF 80Hz + LPF 5kHz)
    compute_shelf(cab_lo_, 80.0f, -12.0f, false);    // 80Hz 以下を緩やかにカット
    compute_shelf(cab_hi_, 5000.0f, -18.0f, true);   // 5kHz 以上をカット

    // テープエコー: ドット8分 @120BPM = 375ms デフォルト
    delay_.delay_samples = 0.375f * sr_;
    delay_.feedback = 0.4f;

    // Space エフェクト デフォルト
    space_wet_ = 0.5f;
    space_wet_target_ = 0.5f;

    // Room reverb 初期設定
    setup_room(0.5f, 0.7f);
    // Plate reverb 初期設定
    setup_plate(0.7f);
}

// ─── メイン処理（信号順: トレモロ→プリアンプ→EQ→コーラス→テープディレイ）──
void FxChain::process(float* buf, int frames)
{
    // MIDIスレッドからのパラメータ変更をオーディオスレッドで適用
    while (auto ev = param_queue_.pop())
        apply_param(ev->cc, ev->value);
    if (trem_.depth > 0.001f)        process_tremolo(buf, frames);
    if (preamp_.drive > 1.01f) {
        process_preamp(buf, frames);
        process_cabinet(buf, frames);
    }
    if (eq_lo_db_ != 0.0f)          process_biquad(eq_lo_, buf, frames);
    if (eq_hi_db_ != 0.0f)          process_biquad(eq_hi_, buf, frames);
    if (chorus_.wet > 0.001f)        process_chorus(buf, frames);
    if (space_wet_ > 0.001f)         process_space(buf, frames);
}

// ─── MIDI CC パラメータ（MIDIスレッドから呼ばれる → キューに push） ──
void FxChain::set_param(int cc, int value)
{
    param_queue_.push({cc, value});
}

// ─── パラメータ適用（オーディオスレッド内で呼ばれる） ──────────
void FxChain::apply_param(int cc, int val)
{
    const float v = val / 127.0f;  // 0.0〜1.0

    switch (cc) {
    // ── Tremolo (CC1-2) ──
    case 1:  trem_.depth = v * 0.8f; break;              // Depth (モジュレーションホイール)
    case 2:  trem_.inc = (0.5 + v * 7.5) / sr_; break;   // Rate 0.5〜8 Hz

    // ── Overdrive (CC70-71) ──
    case 70: preamp_.drive = 1.0f + v * 7.0f; break;     // Drive 1〜8
    case 71: preamp_.tone = v; break;                     // Tone (0=dark, 1=bright)

    // ── EQ (CC72-73) ──
    case 72:
        eq_lo_db_ = (val - 64) * (20.0f / 64.0f);        // Lo EQ -20〜+20 dB @ 150Hz
        compute_shelf(eq_lo_, 150.0f, eq_lo_db_, false);
        break;
    case 73:
        eq_hi_db_ = (val - 64) * (20.0f / 64.0f);        // Hi EQ -20〜+20 dB @ 3kHz
        compute_shelf(eq_hi_, 3000.0f, eq_hi_db_, true);
        break;

    // ── Space エフェクト (CC75-78) ──
    case 75: {                                             // Space モード切替
        SpaceMode new_mode;
        if (val < 32)       new_mode = SpaceMode::OFF;
        else if (val < 64)  new_mode = SpaceMode::TAPE_ECHO;
        else if (val < 96)  new_mode = SpaceMode::ROOM;
        else                new_mode = SpaceMode::PLATE;
        if (new_mode != space_mode_) {
            space_mode_ = new_mode;
            const char* names[] = {"Off", "Tape Echo", "Room Reverb", "Plate Reverb"};
            fprintf(stderr, "[FxChain] Space: %s\n", names[static_cast<int>(new_mode)]);
        }
        break;
    }
    case 76:                                               // Tape: Time / Room: Size
        if (space_mode_ == SpaceMode::TAPE_ECHO) {
            delay_.delay_samples = v * 0.5f * sr_;
        } else if (space_mode_ == SpaceMode::ROOM) {
            room_size_ = v;
            setup_room(v, room_decay_);
        }
        break;
    case 77:                                               // Tape: Feedback / Reverb: Decay
        if (space_mode_ == SpaceMode::TAPE_ECHO) {
            delay_.feedback = v * 0.85f;
        } else if (space_mode_ == SpaceMode::ROOM) {
            room_decay_ = 0.3f + v * 0.65f;
            setup_room(room_size_, room_decay_);
        } else {
            plate_decay_ = 0.3f + v * 0.65f;
            setup_plate(plate_decay_);
        }
        break;
    case 78:                                               // Wet レベル（共通）
        space_wet_target_ = v;
        space_wet_ = v;
        break;

    // ── Chorus (CC79-81) ──
    case 79: chorus_.inc = (0.5 + v * 1.5) / sr_; break;  // Rate 0.5〜2 Hz
    case 80: chorus_.depth_ms = v * 20.0f; break;          // Depth 0〜20 ms
    case 81: chorus_.wet = v; break;                        // Wet レベル
    }
}

// ─── トレモロ（ステレオ AM, L/R 90° 位相差） ──────────────────
void FxChain::process_tremolo(float* buf, int frames)
{
    for (int i = 0; i < frames; ++i) {
        float lfo_l = 0.5f + 0.5f * lut(trem_.phase);
        float lfo_r = 0.5f + 0.5f * lut(trem_.phase + 0.25);  // 90°
        float mod_l = 1.0f - trem_.depth + trem_.depth * lfo_l;
        float mod_r = 1.0f - trem_.depth + trem_.depth * lfo_r;
        buf[i * 2 + 0] *= mod_l;
        buf[i * 2 + 1] *= mod_r;
        trem_.phase += trem_.inc;
        if (trem_.phase >= 1.0) trem_.phase -= 1.0;
    }
}

// ─── オーバードライブ（2段カスケード + 2x オーバーサンプリング） ──
// Stage 1: プリアンプ段（緩い非対称サチュレーション）
// Stage 2: パワーアンプ段（ソフトクリップ）
// 2x オーバーサンプリングでエイリアシングを除去
//
// 非対称クリッピング → 偶数次倍音（真空管 / Suitcase amp の温かみ）

// 1段分のサチュレーション（インライン）
static inline float saturate_asym(float x)
{
    if (x >= 0.0f) {
        // 正側: ソフトクリップ（3次多項式）
        if (x < 1.5f)
            return x - 0.148f * x * x * x;  // 緩やかに飽和
        else
            return 1.17f;
    } else {
        // 負側: tanh でソフトに（非対称 = 偶数次倍音）
        return tanhf(x * 0.7f) * 1.43f;
    }
}

void FxChain::process_preamp(float* buf, int frames)
{
    const float d = preamp_.drive;
    // Stage 1 ゲイン（プリ段: drive の 60%）、Stage 2（パワー段: drive の 40%）
    const float d1 = 1.0f + (d - 1.0f) * 0.6f;
    const float d2 = 1.0f + (d - 1.0f) * 0.4f;
    // tone: LPF 係数（drive が上がるとデフォルトで暗くなる）
    const float lpf_k = 0.2f + preamp_.tone * 0.6f;

    // 自動ゲイン補正: 基準レベル(0.3)を2段サチュレーションに通して
    // 出力がどれだけ変わるかを計算し、その逆数で補正
    const float ref = 0.3f;
    float ref_out = saturate_asym(ref * d1);
    ref_out = saturate_asym(ref_out * d2);
    const float compensation = ref / (ref_out + 1e-6f);

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float x = buf[i * 2 + ch];

            // ── 2x アップサンプル（ゼロ挿入 + 補間） ──
            float& up_prev = (ch == 0) ? preamp_.up_prev_l : preamp_.up_prev_r;
            float s0 = (x + up_prev) * 0.5f;
            float s1 = x;
            up_prev = x;

            // ── 2段カスケードサチュレーション（2x レートで処理） ──
            float out = 0.0f;
            for (int os = 0; os < 2; ++os) {
                float in = (os == 0) ? s0 : s1;

                // Stage 1: プリアンプ段
                float y = saturate_asym(in * d1);

                // Stage 2: パワーアンプ段
                y = saturate_asym(y * d2);

                out += y;
            }
            // ── 2x ダウンサンプル（平均） + ゲイン補正 ──
            out *= 0.5f * compensation;

            // ポストドライブ トーンフィルタ（1次 LPF）
            float& lpf = (ch == 0) ? preamp_.lpf_l : preamp_.lpf_r;
            lpf += lpf_k * (out - lpf);
            buf[i * 2 + ch] = lpf;
        }
    }
}

// ─── キャビネットシミュレーション（Suitcase スピーカー風） ────────
// Drive が ON のときだけ呼ばれる。200Hz HPF + 5kHz LPF で
// ダイレクト音の硬さを除去し「アンプから鳴ってる」感を出す
void FxChain::process_cabinet(float* buf, int frames)
{
    process_biquad(cab_lo_, buf, frames);
    process_biquad(cab_hi_, buf, frames);
}

// ─── EQ バイクアッド (Direct Form II Transposed) ──────────────
void FxChain::compute_shelf(Biquad& bq, float fc, float gain_db, bool high)
{
    // Audio EQ Cookbook: Robert Bristow-Johnson
    const float A  = std::pow(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * fc / sr_;
    const float cs = std::cos(w0);
    const float sn = std::sin(w0);
    const float S  = 1.0f;  // shelf slope
    const float alpha = sn / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/S - 1.0f) + 2.0f);
    const float twoSqrtAalpha = 2.0f * std::sqrt(A) * alpha;

    float a0;
    if (high) {
        bq.b0 =      A * ((A+1) + (A-1)*cs + twoSqrtAalpha);
        bq.b1 = -2 * A * ((A-1) + (A+1)*cs);
        bq.b2 =      A * ((A+1) + (A-1)*cs - twoSqrtAalpha);
        a0    =            (A+1) - (A-1)*cs + twoSqrtAalpha;
        bq.a1 =      2  * ((A-1) - (A+1)*cs);
        bq.a2 =            (A+1) - (A-1)*cs - twoSqrtAalpha;
    } else {
        bq.b0 =      A * ((A+1) - (A-1)*cs + twoSqrtAalpha);
        bq.b1 =  2 * A * ((A-1) - (A+1)*cs);
        bq.b2 =      A * ((A+1) - (A-1)*cs - twoSqrtAalpha);
        a0    =            (A+1) + (A-1)*cs + twoSqrtAalpha;
        bq.a1 =     -2  * ((A-1) + (A+1)*cs);
        bq.a2 =            (A+1) + (A-1)*cs - twoSqrtAalpha;
    }
    // 正規化
    bq.b0 /= a0; bq.b1 /= a0; bq.b2 /= a0;
    bq.a1 /= a0; bq.a2 /= a0;
}

void FxChain::process_biquad(Biquad& bq, float* buf, int frames)
{
    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float x = buf[i * 2 + ch];
            float y = bq.b0 * x + bq.s1[ch];
            bq.s1[ch] = bq.b1 * x - bq.a1 * y + bq.s2[ch];
            bq.s2[ch] = bq.b2 * x - bq.a2 * y;
            buf[i * 2 + ch] = y;
        }
    }
}

// ─── コーラス（LFO 変調ディレイ + dry/wet） ──────────────────
void FxChain::process_chorus(float* buf, int frames)
{
    const float base_delay = sr_ * 0.007f;  // 7ms ベース遅延
    const float depth_samp = chorus_.depth_ms * 0.001f * sr_;

    for (int i = 0; i < frames; ++i) {
        const float dry_l = buf[i * 2 + 0];
        const float dry_r = buf[i * 2 + 1];

        chorus_.buf_l[chorus_.write] = dry_l;
        chorus_.buf_r[chorus_.write] = dry_r;

        // LFO → 遅延量
        auto read_delayed = [&](const float* dbuf, double phase) -> float {
            float d = base_delay + depth_samp * (0.5f + 0.5f * lut(phase));
            int d0 = static_cast<int>(d);
            float frac = d - static_cast<float>(d0);
            int r0 = (chorus_.write - d0)     & (CHORUS_BUF - 1);
            int r1 = (chorus_.write - d0 - 1) & (CHORUS_BUF - 1);
            return dbuf[r0] * (1.0f - frac) + dbuf[r1] * frac;
        };

        float wet_l = read_delayed(chorus_.buf_l, chorus_.phase_l);
        float wet_r = read_delayed(chorus_.buf_r, chorus_.phase_r);

        buf[i * 2 + 0] = dry_l * (1.0f - chorus_.wet) + wet_l * chorus_.wet;
        buf[i * 2 + 1] = dry_r * (1.0f - chorus_.wet) + wet_r * chorus_.wet;

        chorus_.write = (chorus_.write + 1) & (CHORUS_BUF - 1);
        chorus_.phase_l += chorus_.inc;
        chorus_.phase_r += chorus_.inc;
        if (chorus_.phase_l >= 1.0) chorus_.phase_l -= 1.0;
        if (chorus_.phase_r >= 1.0) chorus_.phase_r -= 1.0;
    }
}

// ─── Space エフェクト ディスパッチ ──────────────────────────
void FxChain::process_space(float* buf, int frames)
{
    switch (space_mode_) {
    case SpaceMode::OFF:
        break;
    case SpaceMode::TAPE_ECHO:
        if (delay_.delay_samples > 1.0f) process_tape_echo(buf, frames);
        break;
    case SpaceMode::ROOM:
        process_room(buf, frames);
        break;
    case SpaceMode::PLATE:
        process_plate(buf, frames);
        break;
    }
}

// ─── テープエコー（サチュレーション + ウォーム LPF + Wet/Dry） ──
void FxChain::process_tape_echo(float* buf, int frames)
{
    // テープ風パラメータ
    const float lpf_k = 0.15f;  // ウォームな LPF（≈1.5kHz カットオフ — テープヘッドの高域ロス）
    const float sat_drive = 1.5f;  // テープサチュレーション量

    for (int i = 0; i < frames; ++i) {
        const float dry_l = buf[i * 2 + 0];
        const float dry_r = buf[i * 2 + 1];

        // 遅延読み出し（線形補間）
        float d = delay_.delay_samples;
        int d0 = static_cast<int>(d);
        float frac = d - static_cast<float>(d0);
        int r0 = (delay_.write - d0)     & (DELAY_BUF - 1);
        int r1 = (delay_.write - d0 - 1) & (DELAY_BUF - 1);

        float tap_l = delay_.buf_l[r0] * (1.0f - frac) + delay_.buf_l[r1] * frac;
        float tap_r = delay_.buf_r[r0] * (1.0f - frac) + delay_.buf_r[r1] * frac;

        // テープ風 LPF（フィードバック経路に適用 — 繰り返すごとに高域が落ちる）
        delay_.lpf_l += lpf_k * (tap_l - delay_.lpf_l);
        delay_.lpf_r += lpf_k * (tap_r - delay_.lpf_r);

        // テープサチュレーション（ソフトクリッピング — 繰り返すごとに歪みが増す）
        float fb_l = tanhf(delay_.lpf_l * sat_drive) / sat_drive;
        float fb_r = tanhf(delay_.lpf_r * sat_drive) / sat_drive;

        // フィードバック書き込み
        delay_.buf_l[delay_.write] = dry_l + fb_l * delay_.feedback;
        delay_.buf_r[delay_.write] = dry_r + fb_r * delay_.feedback;

        // 出力: dry + wet（テープエコーらしく wet で混ぜる）
        buf[i * 2 + 0] = dry_l + tap_l * space_wet_;
        buf[i * 2 + 1] = dry_r + tap_r * space_wet_;

        delay_.write = (delay_.write + 1) & (DELAY_BUF - 1);
    }
}

// ─── Room Reverb (Schroeder: 4 comb + 2 allpass) ──────────────
void FxChain::setup_room(float size, float decay)
{
    // コムフィルタ遅延（素数ベース、size でスケール）
    static constexpr int base_comb[] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
    static constexpr int base_ap[]   = {225, 556, 441, 341};
    float scale = 0.5f + size * 1.0f;  // 0.5〜1.5x

    for (int i = 0; i < 4; ++i) {
        comb_l_[i].delay = std::min(static_cast<int>(base_comb[i] * scale), COMB_BUF - 1);
        comb_r_[i].delay = std::min(static_cast<int>(base_comb[i + 4] * scale), COMB_BUF - 1);
        comb_l_[i].feedback = decay;
        comb_r_[i].feedback = decay;
    }
    for (int i = 0; i < 2; ++i) {
        ap_l_[i].delay = std::min(static_cast<int>(base_ap[i] * scale), AP_BUF - 1);
        ap_r_[i].delay = std::min(static_cast<int>(base_ap[i + 2] * scale), AP_BUF - 1);
        ap_l_[i].feedback = 0.5f;
        ap_r_[i].feedback = 0.5f;
    }
}

void FxChain::process_room(float* buf, int frames)
{
    const float lpf_k = 0.3f;  // 高域減衰

    for (int i = 0; i < frames; ++i) {
        float dry_l = buf[i * 2 + 0];
        float dry_r = buf[i * 2 + 1];

        // 4 並列コムフィルタ（LBCF: Lowpass-feedback comb filter）
        float sum_l = 0.0f, sum_r = 0.0f;
        for (int c = 0; c < 4; ++c) {
            // Left
            {
                auto& cf = comb_l_[c];
                int rd = (cf.write - cf.delay) & (COMB_BUF - 1);
                float tap = cf.buf[rd];
                cf.lpf += lpf_k * (tap - cf.lpf);
                cf.buf[cf.write] = dry_l + cf.lpf * cf.feedback;
                cf.write = (cf.write + 1) & (COMB_BUF - 1);
                sum_l += tap;
            }
            // Right
            {
                auto& cf = comb_r_[c];
                int rd = (cf.write - cf.delay) & (COMB_BUF - 1);
                float tap = cf.buf[rd];
                cf.lpf += lpf_k * (tap - cf.lpf);
                cf.buf[cf.write] = dry_r + cf.lpf * cf.feedback;
                cf.write = (cf.write + 1) & (COMB_BUF - 1);
                sum_r += tap;
            }
        }
        sum_l *= 0.25f;
        sum_r *= 0.25f;

        // 2 直列オールパスフィルタ
        for (int a = 0; a < 2; ++a) {
            {
                auto& ap = ap_l_[a];
                int rd = (ap.write - ap.delay) & (AP_BUF - 1);
                float tap = ap.buf[rd];
                float in = sum_l + tap * ap.feedback;
                ap.buf[ap.write] = in;
                ap.write = (ap.write + 1) & (AP_BUF - 1);
                sum_l = tap - in * ap.feedback;
            }
            {
                auto& ap = ap_r_[a];
                int rd = (ap.write - ap.delay) & (AP_BUF - 1);
                float tap = ap.buf[rd];
                float in = sum_r + tap * ap.feedback;
                ap.buf[ap.write] = in;
                ap.write = (ap.write + 1) & (AP_BUF - 1);
                sum_r = tap - in * ap.feedback;
            }
        }

        buf[i * 2 + 0] = dry_l + sum_l * space_wet_;
        buf[i * 2 + 1] = dry_r + sum_r * space_wet_;
    }
}

// ─── Plate Reverb (6 allpass chain — 高密度反射) ──────────────
void FxChain::setup_plate(float decay)
{
    // 素数ベースの遅延で密度の高い反射パターン
    static constexpr int base_delays_l[] = {113, 337, 677, 1049, 1559, 2039};
    static constexpr int base_delays_r[] = {139, 379, 719, 1103, 1613, 2111};

    for (int i = 0; i < 6; ++i) {
        plate_l_[i].delay = std::min(base_delays_l[i], PLATE_AP_BUF - 1);
        plate_r_[i].delay = std::min(base_delays_r[i], PLATE_AP_BUF - 1);
        // 後段ほど feedback を下げてメタリック感を抑える
        float fb = decay * (1.0f - 0.05f * i);
        plate_l_[i].feedback = fb;
        plate_r_[i].feedback = fb;
    }
}

void FxChain::process_plate(float* buf, int frames)
{
    for (int i = 0; i < frames; ++i) {
        float sig_l = buf[i * 2 + 0];
        float sig_r = buf[i * 2 + 1];

        // 6 段オールパスチェーン
        for (int a = 0; a < 6; ++a) {
            {
                auto& ap = plate_l_[a];
                int rd = (ap.write - ap.delay) & (PLATE_AP_BUF - 1);
                float tap = ap.buf[rd];
                float in = sig_l + tap * ap.feedback;
                ap.buf[ap.write] = in;
                ap.write = (ap.write + 1) & (PLATE_AP_BUF - 1);
                sig_l = tap - in * ap.feedback;
            }
            {
                auto& ap = plate_r_[a];
                int rd = (ap.write - ap.delay) & (PLATE_AP_BUF - 1);
                float tap = ap.buf[rd];
                float in = sig_r + tap * ap.feedback;
                ap.buf[ap.write] = in;
                ap.write = (ap.write + 1) & (PLATE_AP_BUF - 1);
                sig_r = tap - in * ap.feedback;
            }
        }

        buf[i * 2 + 0] = buf[i * 2 + 0] * (1.0f - space_wet_) + sig_l * space_wet_;
        buf[i * 2 + 1] = buf[i * 2 + 1] * (1.0f - space_wet_) + sig_r * space_wet_;
    }
}
