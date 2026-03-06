#include "voice.hpp"
#include <cmath>
#include <algorithm>

static constexpr float RELEASE_TIME_S = 0.200f;
static constexpr float kMaxVelocity   = 127.0f;

void Voice::note_on(const SampleData* sd, int note, int vel, int sample_rate)
{
    sample           = sd;
    target_note      = note;
    velocity         = vel;
    is_release_voice = false;
    sustained        = false;
    // double 精度で計算してから float に落とす
    // サンプルレート比を掛けてピッチを補正（例: 44100Hz サンプル → 48000Hz 出力）
    double sr_ratio = (sd->sample_rate > 0 && sample_rate > 0)
                      ? static_cast<double>(sd->sample_rate) / sample_rate
                      : 1.0;
    pitch_ratio  = static_cast<float>(std::pow(2.0, (note - sd->midi_note) / 12.0) * sr_ratio);
    fprintf(stderr, "[Voice] note=%d sd_note=%d sd_sr=%d out_sr=%d sr_ratio=%.4f pitch=%.4f\n",
            note, sd->midi_note, sd->sample_rate, sample_rate, sr_ratio, static_cast<double>(pitch_ratio));
    position     = 0.0;
    gain         = vel / kMaxVelocity;
    release_gain = 1.0f;
    release_rate = 1.0f / (RELEASE_TIME_S * sample_rate);
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

bool Voice::mix(float* buf, int frames)
{
    if (state == State::IDLE || !sample || sample->pcm.empty()) return true;

    const auto& pcm = sample->pcm;
    const double pcm_len_d = static_cast<double>(pcm.size());
    const int    pcm_len   = static_cast<int>(pcm.size());

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

        float s0 = pcm[p0];
        float s1 = (p1 < pcm_len) ? pcm[p1] : 0.0f;
        float sample_val = s0 + frac * (s1 - s0);

        float out = sample_val * gain * release_gain;
        buf[i * 2 + 0] += out;
        buf[i * 2 + 1] += out;

        position += pitch_ratio;
    }

    return false;
}
