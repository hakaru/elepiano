#include "voice.hpp"
#include <cmath>
#include <algorithm>

// リリース時間 (秒)
static constexpr float RELEASE_TIME_S = 0.200f;

void Voice::note_on(const SampleData* sd, int note, int velocity, int sample_rate)
{
    sample       = sd;
    target_note  = note;
    pitch_ratio  = std::pow(2.0f, (note - sd->midi_note) / 12.0f);
    position     = 0.0;
    gain         = velocity / 127.0f;
    release_gain = 1.0f;
    release_rate = 1.0f / (RELEASE_TIME_S * sample_rate);
    state        = State::PLAYING;
}

void Voice::note_off()
{
    if (state == State::PLAYING) {
        state = State::RELEASING;
    }
}

bool Voice::mix(float* buf, int frames)
{
    if (state == State::IDLE || !sample || sample->pcm.empty()) return true;

    const auto& pcm = sample->pcm;
    const int   pcm_len = static_cast<int>(pcm.size());

    for (int i = 0; i < frames; ++i) {
        if (state == State::RELEASING) {
            release_gain -= release_rate;
            if (release_gain <= 0.0f) {
                state = State::IDLE;
                return true;
            }
        }

        // 線形補間リサンプリング
        int    p0  = static_cast<int>(position);
        float  frac = static_cast<float>(position - p0);
        int    p1  = p0 + 1;

        if (p0 >= pcm_len) {
            state = State::IDLE;
            return true;
        }

        float s0 = pcm[p0];
        float s1 = (p1 < pcm_len) ? pcm[p1] : 0.0f;
        float sample_val = s0 + frac * (s1 - s0);

        // ステレオバッファへ書き込み (L/R 同じ値)
        float out = sample_val * gain * release_gain;
        buf[i * 2 + 0] += out;
        buf[i * 2 + 1] += out;

        position += pitch_ratio;
    }

    return false;  // まだ再生中
}
