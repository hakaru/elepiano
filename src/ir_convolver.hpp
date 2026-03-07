#pragma once

#include <string>
#include <vector>

// Cabinet impulse response convolver with multiple IR support.
// Loads multiple IRs (e.g. different mic positions) and allows
// real-time switching and wet/dry control via CC.
class IrConvolver {
public:
    IrConvolver();
    ~IrConvolver();

    // Load a single WAV file (PCM 16/24/32 bit, mono or stereo→summed to mono).
    bool load_wav(const std::string& path);

    // Load all WAV files from a directory (sorted alphabetically).
    // Returns number of IRs loaded.
    int load_dir(const std::string& dir_path);

    // Initialize from environment variables:
    //   ELEPIANO_IR_DIR   — directory of WAV IRs (preferred)
    //   ELEPIANO_IR_FILE  — single WAV IR (fallback)
    //   ELEPIANO_IR_WET   — wet mix 0.0-1.0 (default 0.7)
    bool initialize_from_env();

    // Process interleaved stereo buffer in-place.
    void process(float* interleaved_stereo, int frames);

    // Set wet/dry mix (0.0 = fully dry / OFF, 1.0 = fully wet)
    void set_wet(float w);

    // Select active IR by index (0-based). Clamped to valid range.
    void select(int index);

    bool is_loaded() const { return !irs_.empty(); }
    int ir_count() const { return static_cast<int>(irs_.size()); }
    int active_index() const { return active_; }
    const std::string& active_name() const;

private:
    static constexpr int MAX_IR_LEN = 4096;

    struct IrSlot {
        std::string name;           // filename without path
        std::vector<float> data;    // mono IR coefficients (peak-normalized)
    };
    std::vector<IrSlot> irs_;
    int active_ = 0;
    float wet_ = 0.7f;

    // Circular buffer per channel (sized to max IR length across all slots)
    struct ChannelState {
        std::vector<float> ring;
        int write_pos = 0;
    };
    ChannelState ch_[2];
    int ring_size_ = 0;   // size of circular buffer (power of 2)
    int ring_mask_ = 0;   // ring_size_ - 1 (bitmask for modulo)

    // Internal: load a WAV and return normalized float samples
    bool load_wav_internal(const std::string& path, IrSlot& slot);
    void resize_ring();
};
