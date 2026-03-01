#include "audio_output.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>

AudioOutput::AudioOutput(const Config& cfg) : cfg_(cfg)
{
    open_pcm();
    float_buf_.resize(cfg_.period_size * cfg_.channels);
    s16_buf_.resize(cfg_.period_size * cfg_.channels);
}

AudioOutput::~AudioOutput()
{
    if (pcm_) {
        snd_pcm_drain(pcm_);
        snd_pcm_close(pcm_);
    }
}

void AudioOutput::open_pcm()
{
    int err;
    if ((err = snd_pcm_open(&pcm_, cfg_.device.c_str(),
                            SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        throw std::runtime_error(
            std::string("ALSA PCM open 失敗: ") + cfg_.device +
            " (" + snd_strerror(err) + ")");
    }

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm_, hw);

    auto chk = [&](int r, const char* msg) {
        if (r < 0) throw std::runtime_error(
            std::string(msg) + ": " + snd_strerror(r));
    };

    chk(snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED), "access");
    chk(snd_pcm_hw_params_set_format(pcm_, hw, SND_PCM_FORMAT_S16_LE),         "format");

    unsigned int rate = cfg_.sample_rate;
    chk(snd_pcm_hw_params_set_rate_near(pcm_, hw, &rate, nullptr),             "rate");
    cfg_.sample_rate = rate;

    chk(snd_pcm_hw_params_set_channels(pcm_, hw, cfg_.channels),               "channels");

    snd_pcm_uframes_t period = cfg_.period_size;
    chk(snd_pcm_hw_params_set_period_size_near(pcm_, hw, &period, nullptr),    "period_size");
    cfg_.period_size = period;

    unsigned int periods = cfg_.periods;
    chk(snd_pcm_hw_params_set_periods_near(pcm_, hw, &periods, nullptr),       "periods");

    chk(snd_pcm_hw_params(pcm_, hw), "hw_params");

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm_, sw);
    snd_pcm_sw_params_set_start_threshold(pcm_, sw, period);
    snd_pcm_sw_params(pcm_, sw);

    printf("[AudioOutput] device=%s rate=%u period=%lu channels=%d\n",
           cfg_.device.c_str(), rate,
           static_cast<unsigned long>(period), cfg_.channels);
}

void AudioOutput::run()
{
    running_ = true;
    const int frames = cfg_.period_size;

    while (running_) {
        if (fill_cb_) {
            fill_cb_(float_buf_.data(), frames);
        } else {
            std::fill(float_buf_.begin(), float_buf_.end(), 0.0f);
        }

        // float → S16LE 変換 + クリップ
        for (int i = 0; i < frames * cfg_.channels; ++i) {
            float v = float_buf_[i];
            v = std::max(-1.0f, std::min(1.0f, v));
            s16_buf_[i] = static_cast<int16_t>(v * 32767.0f);
        }

        int ret = snd_pcm_writei(pcm_, s16_buf_.data(), frames);
        if (ret == -EPIPE) {
            // アンダーラン
            snd_pcm_prepare(pcm_);
        } else if (ret < 0) {
            snd_pcm_recover(pcm_, ret, 0);
        }
    }
}
