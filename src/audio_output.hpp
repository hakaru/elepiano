#pragma once
#include <alsa/asoundlib.h>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>

class AudioOutput {
public:
    using FillCallback = std::function<void(float* buf, int frames)>;

    struct Config {
        std::string device      = "default";
        int         sample_rate = 44100;
        int         channels    = 2;
        int         period_size = 256;
        int         periods     = 4;
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

    Config              cfg_;
    snd_pcm_t*          pcm_    = nullptr;
    std::atomic<bool>       running_{false};
    std::atomic<uint32_t>   underrun_count_{0};
    FillCallback            fill_cb_;
    std::vector<float>      float_buf_;
    std::vector<int16_t>    s16_buf_;
};
