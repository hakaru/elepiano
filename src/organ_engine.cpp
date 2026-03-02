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

    // Leslie デフォルト = スロー
    leslie_phase_inc_ = LESLIE_SLOW_HZ / sample_rate_;
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
    n.active = true;
    n.gain   = velocity / 127.0f;

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
    // CC 64: Leslie 速度切替（どのチャンネルからでも）
    if (cc_num == 64) {
        leslie_fast_      = (cc_val >= 64);
        leslie_phase_inc_ = (leslie_fast_ ? LESLIE_FAST_HZ : LESLIE_SLOW_HZ)
                            / sample_rate_;
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

        // 全セクション（Upper / Lower / Pedal）を合算
        for (int s = 0; s < NUM_SECTIONS; ++s) {
            auto& sec = sections_[s];
            if (sec.active_count == 0) continue;

            const float* db_gain = sec_drawbar_gain[s];
            int remaining = sec.active_count;

            for (int n = 0; n < MAX_NOTES && remaining > 0; ++n) {
                auto& ns = sec.notes[n];
                if (!ns.active) continue;
                --remaining;

                float note_s = 0.0f;
                for (int d = 0; d < NUM_DRAWBARS; ++d) {
                    const int lut_idx = static_cast<int>(ns.phase[d] * LUT_SIZE)
                                        & (LUT_SIZE - 1);
                    ns.phase[d] += ns.inc[d];
                    if (ns.phase[d] >= 1.0) ns.phase[d] -= 1.0;

                    if (db_gain[d] == 0.0f) continue;
                    note_s += sin_lut_[lut_idx] * db_gain[d];
                }
                mix_s += note_s * ns.gain * sec.volume;
            }
        }

        // tanhf ソフトクリップ（Hammond アンプの管球飽和、単精度で tanh より高速）
        const float driven = tanhf(mix_s * MASTER_GAIN);

        // Leslie: 90° クォドラチャー AM (出力 ≤ 1.0 を保証)
        // R チャンネルは L から固定オフセット (LUT_SIZE/4 = 1024) で計算
        // → (leslie_phase_ + 0.25) * LUT_SIZE の double 演算を毎サンプル省略
        const int   lut_L  = static_cast<int>(leslie_phase_ * LUT_SIZE)
                             & (LUT_SIZE - 1);
        const int   lut_R  = (lut_L + (LUT_SIZE / 4)) & (LUT_SIZE - 1);
        const float L_gain = 0.75f + 0.25f * sin_lut_[lut_L];
        const float R_gain = 0.75f + 0.25f * sin_lut_[lut_R];

        buf[i * 2]     = driven * L_gain;
        buf[i * 2 + 1] = driven * R_gain;

        leslie_phase_ += leslie_phase_inc_;
        if (leslie_phase_ >= 1.0) leslie_phase_ -= 1.0;
    }
}
