#include "fx_chain.hpp"
#include <cmath>
#include <algorithm>

static constexpr double PI2 = 2.0 * M_PI;

FxChain::FxChain(int sample_rate) : sr_(sample_rate)
{
    for (int i = 0; i < LUT_SIZE; ++i)
        sin_lut_[i] = static_cast<float>(std::sin(PI2 * i / LUT_SIZE));

    // デフォルト LFO レート
    trem_.inc = 2.0 / sr_;   // 2 Hz
    chorus_.inc = 1.0 / sr_; // 1 Hz

    // EQ: フラット初期状態
    compute_shelf(eq_lo_, 200.0f, 0.0f, false);
    compute_shelf(eq_hi_, 4000.0f, 0.0f, true);

    // テープエコー: ドット8分 @120BPM = 375ms デフォルト
    delay_.delay_samples = 0.375f * sr_;
    delay_.feedback = 0.4f;
    delay_.wet = 0.5f;
    delay_.wet_target = 0.5f;
}

// ─── メイン処理（信号順: トレモロ→プリアンプ→EQ→コーラス→テープディレイ）──
void FxChain::process(float* buf, int frames)
{
    // MIDIスレッドからのパラメータ変更をオーディオスレッドで適用
    while (auto ev = param_queue_.pop())
        apply_param(ev->cc, ev->value);
    if (trem_.depth > 0.001f)        process_tremolo(buf, frames);
    if (preamp_.drive > 1.01f)       process_preamp(buf, frames);
    if (eq_lo_db_ != 0.0f)          process_biquad(eq_lo_, buf, frames);
    if (eq_hi_db_ != 0.0f)          process_biquad(eq_hi_, buf, frames);
    if (chorus_.wet > 0.001f)        process_chorus(buf, frames);
    if (delay_.delay_samples > 1.0f && delay_.wet > 0.001f) process_delay(buf, frames);
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
        eq_lo_db_ = (val - 64) * (12.0f / 64.0f);        // Lo EQ -12〜+12 dB
        compute_shelf(eq_lo_, 200.0f, eq_lo_db_, false);
        break;
    case 73:
        eq_hi_db_ = (val - 64) * (12.0f / 64.0f);        // Hi EQ -12〜+12 dB
        compute_shelf(eq_hi_, 4000.0f, eq_hi_db_, true);
        break;

    // ── Tape Echo (CC75-77) ──
    case 75:                                               // Delay Time 0〜500ms
        delay_.delay_samples = v * 0.5f * sr_;
        delay_.wet = (v > 0.01f) ? delay_.wet_target : 0.0f;
        break;
    case 76: delay_.feedback = v * 0.85f; break;          // Feedback 0〜0.85
    case 77: delay_.wet_target = v; delay_.wet = v; break; // Wet レベル

    // ── Chorus (CC78-80) ──
    case 78: chorus_.inc = (0.5 + v * 1.5) / sr_; break;  // Rate 0.5〜2 Hz
    case 79: chorus_.depth_ms = v * 20.0f; break;          // Depth 0〜20 ms
    case 80: chorus_.wet = v; break;                        // Wet レベル
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

// ─── オーバードライブ（非対称クリッピング + トーンフィルタ） ─────
// Rhodes Suitcase amp 風: 正側はハードクリップ気味、負側はソフトに潰れる
// 偶数次倍音が出て「品がない」温かみのある歪み
void FxChain::process_preamp(float* buf, int frames)
{
    const float d = preamp_.drive;
    const float inv_d = 1.0f / d;
    // tone: LPF 係数（drive が上がるとデフォルトで暗くなる = アンプの特性）
    const float lpf_k = 0.2f + preamp_.tone * 0.6f;  // 0.2〜0.8

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float x = buf[i * 2 + ch] * d;

            // 非対称クリッピング: 正側はハード、負側はソフト
            float y;
            if (x >= 0.0f) {
                // 正側: ハードクリップ寄り（3次多項式）
                if (x < 1.0f)
                    y = x - 0.333f * x * x * x;
                else
                    y = 0.667f;
            } else {
                // 負側: tanhf でソフトに（非対称 = 偶数次倍音）
                y = tanhf(x * 0.8f) * 1.25f;
            }

            // ゲイン補正
            y *= inv_d;

            // ポストドライブ トーンフィルタ（1次 LPF）
            float& lpf = (ch == 0) ? preamp_.lpf_l : preamp_.lpf_r;
            lpf += lpf_k * (y - lpf);
            buf[i * 2 + ch] = lpf;
        }
    }
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

// ─── テープエコー（サチュレーション + ウォーム LPF + Wet/Dry） ──
void FxChain::process_delay(float* buf, int frames)
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
        buf[i * 2 + 0] = dry_l + tap_l * delay_.wet;
        buf[i * 2 + 1] = dry_r + tap_r * delay_.wet;

        delay_.write = (delay_.write + 1) & (DELAY_BUF - 1);
    }
}
