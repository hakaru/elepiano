#include "audio_output.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdio>
#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

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
    close_wav_dump();
    if (pcm_) {
        snd_pcm_drain(pcm_);
        snd_pcm_close(pcm_);
    }
}

void AudioOutput::open_wav_dump()
{
    if (cfg_.wav_dump_path.empty()) return;

    wav_file_ = fopen(cfg_.wav_dump_path.c_str(), "wb");
    if (!wav_file_) {
        fprintf(stderr, "[AudioOutput] WAV dump open 失敗: %s\n", cfg_.wav_dump_path.c_str());
        return;
    }

    // WAV ヘッダ（data_size=0 仮置き、close 時に書き戻す）
    uint32_t data_size = 0;
    uint32_t file_size = 36;  // 仮
    uint16_t audio_fmt = 1;   // PCM
    uint16_t ch = static_cast<uint16_t>(cfg_.channels);
    uint32_t sr = static_cast<uint32_t>(cfg_.sample_rate);
    uint32_t byte_rate = sr * ch * 2;
    uint16_t block_align = ch * 2;
    uint16_t bits = 16;
    uint32_t fmt_size = 16;

    fwrite("RIFF", 1, 4, wav_file_);
    fwrite(&file_size, 4, 1, wav_file_);
    fwrite("WAVE", 1, 4, wav_file_);
    fwrite("fmt ", 1, 4, wav_file_);
    fwrite(&fmt_size, 4, 1, wav_file_);
    fwrite(&audio_fmt, 2, 1, wav_file_);
    fwrite(&ch, 2, 1, wav_file_);
    fwrite(&sr, 4, 1, wav_file_);
    fwrite(&byte_rate, 4, 1, wav_file_);
    fwrite(&block_align, 2, 1, wav_file_);
    fwrite(&bits, 2, 1, wav_file_);
    fwrite("data", 1, 4, wav_file_);
    fwrite(&data_size, 4, 1, wav_file_);

    wav_data_bytes_ = 0;
    fprintf(stderr, "[AudioOutput] WAV dump: %s\n", cfg_.wav_dump_path.c_str());
}

void AudioOutput::close_wav_dump()
{
    if (!wav_file_) return;

    // ヘッダのサイズフィールドを書き戻す
    uint32_t file_size = 36 + wav_data_bytes_;
    fseek(wav_file_, 4, SEEK_SET);
    fwrite(&file_size, 4, 1, wav_file_);
    fseek(wav_file_, 40, SEEK_SET);
    fwrite(&wav_data_bytes_, 4, 1, wav_file_);

    fclose(wav_file_);
    wav_file_ = nullptr;
    fprintf(stderr, "[AudioOutput] WAV dump 完了: %u bytes\n", wav_data_bytes_);
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

    // ALSA が実際に確定した値を取得
    snd_pcm_hw_params_get_periods(hw, &periods, nullptr);
    snd_pcm_uframes_t buffer_size = 0;
    snd_pcm_hw_params_get_buffer_size(hw, &buffer_size);

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm_, sw);
    snd_pcm_sw_params_set_start_threshold(pcm_, sw, period);
    snd_pcm_sw_params(pcm_, sw);

    fprintf(stderr, "[AudioOutput] device=%s rate=%u period=%d periods=%u buffer=%lu channels=%d\n",
           cfg_.device.c_str(), rate, cfg_.period_size, periods,
           static_cast<unsigned long>(buffer_size), cfg_.channels);

    open_wav_dump();
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

        // float → S16LE 変換（ARM では NEON SIMD で 4 サンプル並列処理）
#ifdef __ARM_NEON__
        {
            const int        total   = frames * cfg_.channels;
            const float32x4_t scale   = vdupq_n_f32(32767.0f);
            const float32x4_t clip_lo = vdupq_n_f32(-1.0f);
            const float32x4_t clip_hi = vdupq_n_f32(1.0f);
            int i = 0;
            for (; i + 4 <= total; i += 4) {
                float32x4_t v = vld1q_f32(&float_buf_[i]);
                v = vmaxq_f32(v, clip_lo);
                v = vminq_f32(v, clip_hi);
                v = vmulq_f32(v, scale);
                int32x4_t  iv = vcvtq_s32_f32(v);
                int16x4_t  sv = vqmovn_s32(iv);
                vst1_s16(&s16_buf_[i], sv);
            }
            for (; i < total; ++i) {
                float v = std::clamp(float_buf_[i], -1.0f, 1.0f);
                s16_buf_[i] = static_cast<int16_t>(v * 32767.0f);
            }
        }
#else
        for (int i = 0; i < frames * cfg_.channels; ++i) {
            float v = std::clamp(float_buf_[i], -1.0f, 1.0f);
            s16_buf_[i] = static_cast<int16_t>(v * 32767.0f);
        }
#endif

        // WAV ダンプ
        if (wav_file_) {
            size_t n = static_cast<size_t>(frames * cfg_.channels);
            fwrite(s16_buf_.data(), sizeof(int16_t), n, wav_file_);
            wav_data_bytes_ += static_cast<uint32_t>(n * sizeof(int16_t));
        }

        // 部分書き込みも含めてリトライ
        int written = 0;
        while (written < frames && running_.load()) {
            int ret = snd_pcm_writei(pcm_,
                                     s16_buf_.data() + written * cfg_.channels,
                                     frames - written);
            if (ret == -EPIPE) {
                underrun_count_.fetch_add(1, std::memory_order_relaxed);
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
