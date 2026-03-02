#include "organ_engine.hpp"
#include <cstring>
#include <cmath>

static constexpr double PI2 = 2.0 * M_PI;

// デフォルトドローバー
// Upper: 8'(8) + 4'(8) + 2⅔'(6)  — 明るいリードオルガン音色
// Lower: 8'(8) + 4'(8)             — マイルドな伴奏音色
// Pedal: 16'(8) + 8'(8)            — 重低音ベース
static constexpr int DEFAULT_UPPER[OrganEngine::NUM_DRAWBARS] = { 0,0,8,8,6,0,0,0,0 };
static constexpr int DEFAULT_LOWER[OrganEngine::NUM_DRAWBARS] = { 0,0,8,8,0,0,0,0,0 };
static constexpr int DEFAULT_PEDAL[OrganEngine::NUM_DRAWBARS] = { 8,0,8,0,0,0,0,0,0 };

OrganEngine::OrganEngine(int sample_rate)
    : sample_rate_(sample_rate)
{
    // セクション初期化
    const int*  defs[NUM_SECTIONS] = { DEFAULT_UPPER, DEFAULT_LOWER, DEFAULT_PEDAL };
    for (int s = 0; s < NUM_SECTIONS; ++s) {
        sections_[s].ch     = s;       // CH1=0, CH2=1, CH3=2
        sections_[s].volume = 1.0f;
        for (int d = 0; d < NUM_DRAWBARS; ++d) {
            sections_[s].drawbars[d] = defs[s][d];
        }
    }

    // sin LUT 構築
    for (int i = 0; i < LUT_SIZE; ++i) {
        sin_lut_[i] = static_cast<float>(std::sin(PI2 * i / LUT_SIZE));
    }

    // Leslie デフォルト = スロー（モーター停止位置から）
    leslie_current_hz_ = LESLIE_SLOW_HZ;
    leslie_target_hz_  = LESLIE_SLOW_HZ;

    // キーオンクリック減衰係数: -3 dB/τ ≈ 70ms で 1/e^3 ≈ 0.05 に減衰
    click_decay_ = expf(-3.0f / (CLICK_DECAY_MS * 1e-3f * static_cast<float>(sample_rate)));

    // Vibrato/Chorus LFO 増分（毎サンプル加算する位相量）
    vib_lfo_inc_ = VIB_LFO_HZ / sample_rate_;
}

void OrganEngine::push_event(const MidiEvent& ev)
{
    if (!event_queue_.push(ev) && ev.type == MidiEvent::Type::NOTE_OFF) {
        // NOTE_OFF ドロップはゾンビノートを引き起こすため警告
        fprintf(stderr, "[OrganEngine] WARNING: queue full, NOTE_OFF dropped (ch=%d note=%d)\n",
                ev.channel, ev.note);
    }
}

void OrganEngine::set_drawbar(int section, int drawbar_idx, int value)
{
    if (section < 0 || section >= NUM_SECTIONS)    return;
    if (drawbar_idx < 0 || drawbar_idx >= NUM_DRAWBARS) return;
    int& db = sections_[section].drawbars[drawbar_idx];
    db = value < 0 ? 0 : (value > 8 ? 8 : value);
}

OrganEngine::ManualSection* OrganEngine::_section_for_ch(int ch)
{
    for (auto& s : sections_) {
        if (s.ch == ch) return &s;
    }
    return nullptr;
}

void OrganEngine::_note_on(int ch, int note, int velocity)
{
    if (velocity == 0) { _note_off(ch, note); return; }
    if (note < 0 || note >= MAX_NOTES) return;

    auto* sec = _section_for_ch(ch);
    if (!sec) return;

    auto& n  = sec->notes[note];
    if (!n.active) ++sec->active_count;
    n.active    = true;
    n.gain      = velocity / 127.0f;
    n.click_env = 1.0f;  // キーオンクリック起動

    for (int d = 0; d < NUM_DRAWBARS; ++d) {
        const int    semi = note + DRAWBAR_SEMITONES[d];
        const double freq = 440.0 * std::pow(2.0, (semi - 69) / 12.0);
        n.inc[d]   = freq / sample_rate_;
        n.phase[d] = 0.0;
    }
}

void OrganEngine::_note_off(int ch, int note)
{
    if (note < 0 || note >= MAX_NOTES) return;
    auto* sec = _section_for_ch(ch);
    if (sec && sec->notes[note].active) {
        sec->notes[note].active = false;
        --sec->active_count;
    }
}

void OrganEngine::_handle_cc(int ch, int cc_num, int cc_val)
{
    // CC 1: Vibrato/Chorus モード（どのチャンネルからでも）
    // 0-127 → 0-6 (off / V1 / V2 / V3 / C1 / C2 / C3)
    if (cc_num == 1) {
        vib_mode_ = cc_val * 7 / 128;
        return;
    }

    // CC 64: Leslie 速度切替（ターゲットのみ更新 → 指数収束で徐々に変化）
    if (cc_num == 64) {
        leslie_target_hz_ = (cc_val >= 64) ? LESLIE_FAST_HZ : LESLIE_SLOW_HZ;
        return;
    }

    auto* sec = _section_for_ch(ch);
    if (!sec) return;

    // CC 7: マニュアルごとの音量
    if (cc_num == 7) {
        sec->volume = cc_val / 127.0f;
        return;
    }

    // CC 12-20: ドローバー 0-8（チャンネルで振り分け）
    if (cc_num >= 12 && cc_num <= 20) {
        const int idx = cc_num - 12;
        const int val = (cc_val * 8 + 63) / 127;
        sec->drawbars[idx] = val < 0 ? 0 : (val > 8 ? 8 : val);
    }
}

void OrganEngine::mix(float* buf, int frames)
{
    // MIDI イベント処理（オーディオスレッド）
    while (auto ev = event_queue_.pop()) {
        switch (ev->type) {
        case MidiEvent::Type::NOTE_ON:
            _note_on(ev->channel, ev->note, ev->velocity);
            break;
        case MidiEvent::Type::NOTE_OFF:
            _note_off(ev->channel, ev->note);
            break;
        case MidiEvent::Type::CC:
            _handle_cc(ev->channel, ev->note, ev->velocity);
            break;
        default:
            break;
        }
    }

    std::memset(buf, 0, static_cast<std::size_t>(frames) * 2 * sizeof(float));

    // セクションごとのドローバーゲインをプリコンピュート
    // （内ループでの int→float 除算を排除）
    float sec_drawbar_gain[NUM_SECTIONS][NUM_DRAWBARS];
    for (int s = 0; s < NUM_SECTIONS; ++s) {
        for (int d = 0; d < NUM_DRAWBARS; ++d) {
            sec_drawbar_gain[s][d] = sections_[s].drawbars[d] / 8.0f;
        }
    }

    for (int i = 0; i < frames; ++i) {
        float mix_s = 0.0f;

        // ── [1] 全セクション（Upper / Lower / Pedal）を合算 ──────────────────
        for (int s = 0; s < NUM_SECTIONS; ++s) {
            auto& sec = sections_[s];
            if (sec.active_count == 0) continue;

            const float* db_gain  = sec_drawbar_gain[s];
            int remaining = sec.active_count;

            for (int n = 0; n < MAX_NOTES && remaining > 0; ++n) {
                auto& ns = sec.notes[n];
                if (!ns.active) continue;
                --remaining;

                float note_s = 0.0f;

                // キーオンクリック: 8' 基音位相 (DRAWBAR_SEMITONES[2]=0) の 6 倍音
                if (ns.click_env > 1e-4f) {
                    const int click_lut = static_cast<int>(ns.phase[2] * 6.0 * LUT_SIZE)
                                          & (LUT_SIZE - 1);
                    note_s += CLICK_AMPLITUDE * sin_lut_[click_lut] * ns.click_env;
                    ns.click_env *= click_decay_;
                    if (ns.click_env < 1e-4f) ns.click_env = 0.0f;
                }

                // ドローバー合成（トーンホイール磁束高調波込み）
                for (int d = 0; d < NUM_DRAWBARS; ++d) {
                    if (db_gain[d] == 0.0f) {
                        // 位相は進めないとピッチが狂うので更新だけ行う
                        ns.phase[d] += ns.inc[d];
                        if (ns.phase[d] >= 1.0) ns.phase[d] -= 1.0;
                        continue;
                    }

                    const int lut_idx = static_cast<int>(ns.phase[d] * LUT_SIZE)
                                        & (LUT_SIZE - 1);
                    ns.phase[d] += ns.inc[d];
                    if (ns.phase[d] >= 1.0) ns.phase[d] -= 1.0;

                    // 2倍音・3倍音を加えた磁束波形
                    const int lut_h2 = (lut_idx * 2) & (LUT_SIZE - 1);
                    const int lut_h3 = (lut_idx * 3) & (LUT_SIZE - 1);
                    note_s += (sin_lut_[lut_idx]
                             + TW_H2 * sin_lut_[lut_h2]
                             + TW_H3 * sin_lut_[lut_h3]) * db_gain[d];
                }
                mix_s += note_s * ns.gain * sec.volume;
            }
        }

        // ── [2] tanhf ソフトクリップ（Hammond アンプの管球飽和）──────────────
        const float driven = tanhf(mix_s * MASTER_GAIN);

        // ── [3] Vibrato / Chorus スキャナー ──────────────────────────────────
        float processed;
        if (vib_mode_ == 0) {
            // off: バイパス（遅延バッファへの書き込みはスキップ）
            processed = driven;
        } else {
            // LFO 値: 0.0 〜 1.0
            const float lfo_val = 0.5f + 0.5f * sin_lut_[
                static_cast<int>(vib_lfo_phase_ * LUT_SIZE) & (LUT_SIZE - 1)];
            const float delay_samp = lfo_val * VIB_DEPTHS[vib_mode_];
            const int   d_int  = static_cast<int>(delay_samp);
            const float d_frac = delay_samp - static_cast<float>(d_int);

            vib_buf_[vib_write_pos_] = driven;
            const int r0 = (vib_write_pos_ - d_int)     & (VIB_BUF_SIZE - 1);
            const int r1 = (vib_write_pos_ - d_int - 1) & (VIB_BUF_SIZE - 1);
            const float delayed = vib_buf_[r0] * (1.0f - d_frac)
                                + vib_buf_[r1] * d_frac;

            // V1-V3: 100% wet（純粋なビブラート）
            // C1-C3: 50% dry + 50% wet（コーラス = ユニゾン厚み）
            processed = (vib_mode_ <= 3) ? delayed
                                         : 0.5f * driven + 0.5f * delayed;

            vib_write_pos_ = (vib_write_pos_ + 1) & (VIB_BUF_SIZE - 1);
        }

        // LFO 位相更新
        vib_lfo_phase_ += vib_lfo_inc_;
        if (vib_lfo_phase_ >= 1.0) vib_lfo_phase_ -= 1.0;

        // ── [4] Leslie AM: L/R 90° クォドラチャー ──────────────────────────
        // R チャンネルは L から固定オフセット (LUT_SIZE/4 = 1024) で計算
        const int   lut_L  = static_cast<int>(leslie_phase_ * LUT_SIZE)
                             & (LUT_SIZE - 1);
        const int   lut_R  = (lut_L + (LUT_SIZE / 4)) & (LUT_SIZE - 1);
        const float L_gain = 0.75f + 0.25f * sin_lut_[lut_L];
        const float R_gain = 0.75f + 0.25f * sin_lut_[lut_R];

        // ── [5] ステレオ出力 ─────────────────────────────────────────────────
        buf[i * 2]     = processed * L_gain;
        buf[i * 2 + 1] = processed * R_gain;

        // ── [6] Leslie 回転速度を指数収束で更新（モーター慣性モデル）─────────
        const double tau = (leslie_target_hz_ > leslie_current_hz_)
            ? LESLIE_HORN_ACCEL_TAU : LESLIE_HORN_DECEL_TAU;
        leslie_current_hz_ += (leslie_target_hz_ - leslie_current_hz_)
                              / (tau * sample_rate_);
        leslie_phase_ += leslie_current_hz_ / sample_rate_;
        if (leslie_phase_ >= 1.0) leslie_phase_ -= 1.0;
    }
}
