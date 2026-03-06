#pragma once

#ifdef ELEPIANO_ENABLE_LV2

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class Lv2Host {
public:
    Lv2Host(int sample_rate, int max_block_frames);
    ~Lv2Host();

    bool initialize_from_env();
    void process(float* interleaved_stereo, int frames);
    void set_cc(int cc, float normalized);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

#endif

