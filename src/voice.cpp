#include "voice.hpp"
#include <cmath>
#include <algorithm>

static constexpr float kMaxVelocity = 127.0f;

void Voice::note_on(const SampleData* sd, int note, int vel, int sample_rate,
                    float release_time_s)
{
    sample           = sd;
    target_note      = note;
    velocity         = vel;
    is_release_voice = false;
    sustained        = false;
    double sr_ratio = (sd->sample_rate > 0 && sample_rate > 0)
                      ? static_cast<double>(sd->sample_rate) / sample_rate
                      : 1.0;
    pitch_ratio  = static_cast<float>(std::pow(2.0, (note - sd->midi_note) / 12.0) * sr_ratio);
    position     = 0.0;
    gain         = vel / kMaxVelocity;
    release_gain = 1.0f;
    release_rate = (release_time_s > 0.001f)
                   ? 1.0f / (release_time_s * sample_rate)
                   : 1.0f;  // 0ms: 即座にリリース完了
    state        = State::PLAYING;
}

void Voice::note_off()
{
    // release voice は note_off に反応しない（サンプル末尾まで自然減衰）
    if (is_release_voice) return;
    if (state == State::PLAYING) {
        state = State::RELEASING;
    }
}

bool Voice::mix(float* buf, int frames, float bend_ratio)
{
    if (state == State::IDLE || !sample || sample->pcm.empty()) return true;

    const auto& pcm = sample->pcm;
    const double pcm_len_d = static_cast<double>(pcm.size());
    const int    pcm_len   = static_cast<int>(pcm.size());
    static constexpr float kInt16ToFloat = 1.0f / 32767.0f;

    for (int i = 0; i < frames; ++i) {
        if (state == State::RELEASING) {
            release_gain -= release_rate;
            if (release_gain <= 0.0f) {
                state = State::IDLE;
                return true;
            }
        }

        // position が pcm の範囲を超えたら終了（INT_MAX 超過によるキャスト UB を防ぐ）
        if (position >= pcm_len_d) {
            state = State::IDLE;
            return true;
        }

        int   p0   = static_cast<int>(position);  // position>=0 かつ <pcm_len が保証済み
        float frac = static_cast<float>(position - p0);
        int   p1   = p0 + 1;

        float s0 = static_cast<float>(pcm[p0]) * kInt16ToFloat;
        float s1 = (p1 < pcm_len) ? static_cast<float>(pcm[p1]) * kInt16ToFloat : 0.0f;
        float sample_val = s0 + frac * (s1 - s0);

        float out = sample_val * gain * release_gain;
        buf[i * 2 + 0] += out;
        buf[i * 2 + 1] += out;

        position += pitch_ratio * bend_ratio;
    }

    return false;
}
