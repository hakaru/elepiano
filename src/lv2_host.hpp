#pragma once

#ifdef ELEPIANO_ENABLE_LV2

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// Forward declare lilv types to allow shared world
typedef struct LilvWorldImpl LilvWorld;

class Lv2Host {
public:
    Lv2Host(int sample_rate, int max_block_frames);
    ~Lv2Host();

    // Legacy: initialize from ELEPIANO_LV2_URI / ELEPIANO_LV2_CC_MAP / ELEPIANO_LV2_WET
    bool initialize_from_env();

    // New: initialize with explicit parameters and optional shared LilvWorld.
    // If world is nullptr, a new one is created internally.
    // cc_map format: "70=2,71=3"
    // defaults format: "port=value,port=value,..." to override plugin defaults
    bool initialize(const std::string& uri, const char* cc_map, float wet,
                    LilvWorld* shared_world = nullptr,
                    const char* defaults = nullptr);

    void process(float* interleaved_stereo, int frames);
    void set_cc(int cc, float normalized);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

#endif

