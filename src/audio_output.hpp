#pragma once
#include <alsa/asoundlib.h>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>
#include <cstdio>

class AudioOutput {
public:
    using FillCallback = std::function<void(float* buf, int frames)>;

    struct Config {
        std::string device      = "default";
        int         sample_rate = 44100;
        int         channels    = 2;
        int         period_size = 256;
        int         periods     = 4;
        std::string wav_dump_path;  // 空なら無効
    };

    AudioOutput() : AudioOutput(Config{}) {}
    explicit AudioOutput(const Config& cfg);
    ~AudioOutput();

    // コピー/ムーブ禁止（ALSA ハンドルを生ポインタで保持するため）
    AudioOutput(const AudioOutput&)            = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&)                 = delete;
    AudioOutput& operator=(AudioOutput&&)      = delete;

    void set_callback(FillCallback cb) { fill_cb_ = std::move(cb); }

    void run();
    void stop() { running_.store(false); }

    int sample_rate() const { return cfg_.sample_rate; }
    uint32_t underrun_count() const { return underrun_count_.load(std::memory_order_relaxed); }

private:
    void open_pcm();

    void open_wav_dump();
    void close_wav_dump();

    Config              cfg_;
    snd_pcm_t*          pcm_    = nullptr;
    FILE*               wav_file_ = nullptr;
    uint32_t            wav_data_bytes_ = 0;
    std::atomic<bool>       running_{false};
    std::atomic<uint32_t>   underrun_count_{0};
    FillCallback            fill_cb_;
    std::vector<float>      float_buf_;
    std::vector<int16_t>    s16_buf_;
};
