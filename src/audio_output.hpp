#pragma once
#include <alsa/asoundlib.h>
#include <string>
#include <functional>

class AudioOutput {
public:
    using FillCallback = std::function<void(float* buf, int frames)>;

    struct Config {
        std::string device      = "hw:pisound";
        int         sample_rate = 44100;
        int         channels    = 2;
        int         period_size = 256;   // frames per period (~5.8ms)
        int         periods     = 3;
    };

    explicit AudioOutput(const Config& cfg = {});
    ~AudioOutput();

    void set_callback(FillCallback cb) { fill_cb_ = std::move(cb); }

    // ブロッキングループ（メインスレッドまたは別スレッド）
    void run();
    void stop() { running_ = false; }

    int sample_rate() const { return cfg_.sample_rate; }

private:
    void open_pcm();

    Config         cfg_;
    snd_pcm_t*     pcm_    = nullptr;
    bool           running_ = false;
    FillCallback   fill_cb_;
    std::vector<float>      float_buf_;
    std::vector<int16_t>    s16_buf_;
};
