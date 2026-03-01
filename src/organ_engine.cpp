#include "organ_engine.hpp"
#include <cstring>
#include <cmath>

static constexpr double PI2 = 2.0 * M_PI;

// デフォルトドローバー: 8'(8), 4'(8), 2⅔'(6) — オルガンらしい基本音色
static constexpr int DEFAULT_DRAWBARS[OrganEngine::NUM_DRAWBARS] = {
    0, 0, 8, 8, 6, 0, 0, 0, 0
};

OrganEngine::OrganEngine(int sample_rate)
    : sample_rate_(sample_rate)
{
    for (int i = 0; i < NUM_DRAWBARS; ++i) {
        drawbars_[i] = DEFAULT_DRAWBARS[i];
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
    event_queue_.push(ev);
}

void OrganEngine::set_drawbar(int idx, int value)
{
    if (idx < 0 || idx >= NUM_DRAWBARS) return;
    drawbars_[idx] = value < 0 ? 0 : (value > 8 ? 8 : value);
}

void OrganEngine::_note_on(int note, int velocity)
{
    if (velocity == 0) { _note_off(note); return; }
    if (note < 0 || note >= MAX_NOTES) return;

    auto& n   = notes_[note];
    n.active  = true;
    n.gain    = velocity / 127.0f;

    // 各ドローバーの位相増分を計算
    for (int d = 0; d < NUM_DRAWBARS; ++d) {
        const int    semi = note + DRAWBAR_SEMITONES[d];
        const double freq = 440.0 * std::pow(2.0, (semi - 69) / 12.0);
        n.inc[d]   = freq / sample_rate_;
        n.phase[d] = 0.0;
    }
}

void OrganEngine::_note_off(int note)
{
    if (note < 0 || note >= MAX_NOTES) return;
    notes_[note].active = false;
}

void OrganEngine::_handle_cc(int cc_num, int cc_val)
{
    // CC 64 (Sustain Pedal): Leslie 速度切替
    if (cc_num == 64) {
        leslie_fast_      = (cc_val >= 64);
        leslie_phase_inc_ = (leslie_fast_ ? LESLIE_FAST_HZ : LESLIE_SLOW_HZ)
                            / sample_rate_;
        return;
    }
    // CC 12-20: ドローバー 0-8 (0-127 → 0-8)
    if (cc_num >= 12 && cc_num <= 20) {
        const int idx = cc_num - 12;
        const int val = (cc_val * 8 + 63) / 127;
        set_drawbar(idx, val);
    }
}

void OrganEngine::mix(float* buf, int frames)
{
    // MIDI イベント処理（オーディオスレッド）
    while (auto ev = event_queue_.pop()) {
        switch (ev->type) {
        case MidiEvent::Type::NOTE_ON:
            _note_on(ev->note, ev->velocity);
            break;
        case MidiEvent::Type::NOTE_OFF:
            _note_off(ev->note);
            break;
        case MidiEvent::Type::CC:
            _handle_cc(ev->note, ev->velocity);
            break;
        default:
            break;
        }
    }

    std::memset(buf, 0, static_cast<std::size_t>(frames) * 2 * sizeof(float));

    for (int i = 0; i < frames; ++i) {
        float mix_s = 0.0f;

        for (int n = 0; n < MAX_NOTES; ++n) {
            auto& ns = notes_[n];
            if (!ns.active) continue;

            float note_s = 0.0f;
            for (int d = 0; d < NUM_DRAWBARS; ++d) {
                // ドローバーが 0 でも位相は進める（コヒーレンス保持）
                const int lut_idx = static_cast<int>(ns.phase[d] * LUT_SIZE)
                                    & (LUT_SIZE - 1);
                ns.phase[d] += ns.inc[d];
                if (ns.phase[d] >= 1.0) ns.phase[d] -= 1.0;

                if (drawbars_[d] == 0) continue;
                note_s += sin_lut_[lut_idx] * (drawbars_[d] / 8.0f);
            }
            mix_s += note_s * ns.gain;
        }

        // tanh ソフトクリップ（Hammond アンプの管球飽和をシミュレート）
        const float driven = std::tanh(mix_s * MASTER_GAIN);

        // Leslie シミュレーション: 90° 位相差クォドラチャー方式
        //   L と R を 90° ずらした AM で常に出力 ≤ 1.0 を保証
        const int lut_L = static_cast<int>(leslie_phase_ * LUT_SIZE)
                          & (LUT_SIZE - 1);
        const int lut_R = static_cast<int>((leslie_phase_ + 0.25) * LUT_SIZE)
                          & (LUT_SIZE - 1);

        // AM: 0.75 + 0.25 * sin → range [0.5, 1.0]（max = 1.0）
        const float L_gain = 0.75f + 0.25f * sin_lut_[lut_L];
        const float R_gain = 0.75f + 0.25f * sin_lut_[lut_R];

        buf[i * 2]     = driven * L_gain;   // max |output| ≤ 1.0
        buf[i * 2 + 1] = driven * R_gain;

        leslie_phase_ += leslie_phase_inc_;
        if (leslie_phase_ >= 1.0) leslie_phase_ -= 1.0;
    }
}
