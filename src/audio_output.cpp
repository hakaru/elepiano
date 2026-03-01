#include "audio_output.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>

AudioOutput::AudioOutput(const Config& cfg) : cfg_(cfg)
{
    try {
        open_pcm();
        // open_pcm() 内で cfg_.period_size が ALSA 実際値に更新されるため、その後で確保する
        float_buf_.resize(cfg_.period_size * cfg_.channels);
        s16_buf_.resize(cfg_.period_size * cfg_.channels);
    } catch (...) {
        // コンストラクタ例外時はデストラクタが呼ばれないため、ここで ALSA を閉じる
        if (pcm_) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
        throw;
    }
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

    auto chk = [&](int r, const char* msg) {
        if (r < 0) throw std::runtime_error(
            std::string(msg) + ": " + snd_strerror(r));
    };

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm_, hw);

    chk(snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED), "access");
    chk(snd_pcm_hw_params_set_format(pcm_, hw, SND_PCM_FORMAT_S16_LE),         "format");

    unsigned int rate = static_cast<unsigned int>(cfg_.sample_rate);
    chk(snd_pcm_hw_params_set_rate_near(pcm_, hw, &rate, nullptr),             "rate");
    cfg_.sample_rate = static_cast<int>(rate);

    chk(snd_pcm_hw_params_set_channels(pcm_, hw, cfg_.channels),               "channels");

    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(cfg_.period_size);
    chk(snd_pcm_hw_params_set_period_size_near(pcm_, hw, &period, nullptr),    "period_size");
    cfg_.period_size = static_cast<int>(period);

    unsigned int periods = static_cast<unsigned int>(cfg_.periods);
    chk(snd_pcm_hw_params_set_periods_near(pcm_, hw, &periods, nullptr),       "periods");

    chk(snd_pcm_hw_params(pcm_, hw), "hw_params");

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm_, sw);
    snd_pcm_sw_params_set_start_threshold(pcm_, sw, period);
    snd_pcm_sw_params(pcm_, sw);

    fprintf(stderr, "[AudioOutput] device=%s rate=%u period=%d channels=%d\n",
           cfg_.device.c_str(), rate, cfg_.period_size, cfg_.channels);
}

void AudioOutput::run()
{
    running_.store(true);
    const int frames = cfg_.period_size;

    while (running_.load()) {
        if (fill_cb_) {
            fill_cb_(float_buf_.data(), frames);
        } else {
            std::fill(float_buf_.begin(), float_buf_.end(), 0.0f);
        }

        // float → S16LE 変換（std::clamp でクリップ）
        for (int i = 0; i < frames * cfg_.channels; ++i) {
            float v = std::clamp(float_buf_[i], -1.0f, 1.0f);
            s16_buf_[i] = static_cast<int16_t>(v * 32767.0f);
        }

        // 部分書き込みも含めてリトライ
        int written = 0;
        while (written < frames && running_.load()) {
            int ret = snd_pcm_writei(pcm_,
                                     s16_buf_.data() + written * cfg_.channels,
                                     frames - written);
            if (ret == -EPIPE) {
                fprintf(stderr, "[AudioOutput] underrun\n");
                snd_pcm_prepare(pcm_);
            } else if (ret < 0) {
                ret = snd_pcm_recover(pcm_, ret, 1);
                if (ret < 0) {
                    fprintf(stderr, "[AudioOutput] snd_pcm_recover 失敗: %s\n",
                            snd_strerror(ret));
                    return;
                }
            } else {
                written += ret;
            }
        }
    }
}
