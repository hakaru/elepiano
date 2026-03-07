#include "ir_convolver.hpp"
#include "rt_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─── Minimal WAV parser (PCM 16/24/32 bit, mono/stereo) ───────

namespace {

struct WavHeader {
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_size = 0;
    bool valid = false;
};

uint16_t read_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t read_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

bool parse_wav(const std::vector<uint8_t>& data, WavHeader& hdr, const uint8_t*& pcm_start) {
    if (data.size() < 44) return false;
    const uint8_t* p = data.data();

    if (std::memcmp(p, "RIFF", 4) != 0) return false;
    if (std::memcmp(p + 8, "WAVE", 4) != 0) return false;

    size_t pos = 12;
    bool found_fmt = false, found_data = false;
    while (pos + 8 <= data.size()) {
        uint32_t chunk_size = read_u32(p + pos + 4);
        if (std::memcmp(p + pos, "fmt ", 4) == 0 && chunk_size >= 16) {
            uint16_t fmt = read_u16(p + pos + 8);
            if (fmt != 1) return false;
            hdr.channels = read_u16(p + pos + 10);
            hdr.sample_rate = read_u32(p + pos + 12);
            hdr.bits_per_sample = read_u16(p + pos + 22);
            found_fmt = true;
        } else if (std::memcmp(p + pos, "data", 4) == 0) {
            hdr.data_size = chunk_size;
            pcm_start = p + pos + 8;
            found_data = true;
        }
        pos += 8 + chunk_size;
        if (pos & 1) ++pos;
    }

    hdr.valid = found_fmt && found_data &&
                (hdr.bits_per_sample == 16 || hdr.bits_per_sample == 24 || hdr.bits_per_sample == 32) &&
                (hdr.channels == 1 || hdr.channels == 2);
    return hdr.valid;
}

std::vector<float> pcm_to_float(const uint8_t* pcm, const WavHeader& hdr) {
    int bytes_per_sample = hdr.bits_per_sample / 8;
    int frame_size = bytes_per_sample * hdr.channels;
    int num_frames = static_cast<int>(hdr.data_size / frame_size);

    std::vector<float> out(num_frames);
    for (int i = 0; i < num_frames; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < hdr.channels; ++ch) {
            const uint8_t* s = pcm + i * frame_size + ch * bytes_per_sample;
            float val = 0.0f;
            if (hdr.bits_per_sample == 16) {
                int16_t v = static_cast<int16_t>(s[0] | (s[1] << 8));
                val = v / 32768.0f;
            } else if (hdr.bits_per_sample == 24) {
                int32_t v = s[0] | (s[1] << 8) | (s[2] << 16);
                if (v & 0x800000) v |= 0xFF000000;
                val = v / 8388608.0f;
            } else if (hdr.bits_per_sample == 32) {
                int32_t v;
                std::memcpy(&v, s, 4);
                val = v / 2147483648.0f;
            }
            sum += val;
        }
        out[i] = (hdr.channels == 2) ? sum * 0.5f : sum;
    }
    return out;
}

} // anonymous namespace

// ─── IrConvolver implementation ───────────────────────────────

static const std::string kEmptyName;

IrConvolver::IrConvolver() = default;
IrConvolver::~IrConvolver() = default;

const std::string& IrConvolver::active_name() const {
    if (active_ >= 0 && active_ < static_cast<int>(irs_.size()))
        return irs_[active_].name;
    return kEmptyName;
}

bool IrConvolver::load_wav_internal(const std::string& path, IrSlot& slot) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> raw(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(raw.data()), size);

    WavHeader hdr;
    const uint8_t* pcm_start = nullptr;
    if (!parse_wav(raw, hdr, pcm_start)) return false;

    slot.data = pcm_to_float(pcm_start, hdr);
    if (static_cast<int>(slot.data.size()) > MAX_IR_LEN)
        slot.data.resize(MAX_IR_LEN);
    if (slot.data.empty()) return false;

    // Normalize peak to 1.0
    float peak = 0.0f;
    for (float s : slot.data) peak = std::max(peak, std::fabs(s));
    if (peak > 1e-6f) {
        float inv = 1.0f / peak;
        for (float& s : slot.data) s *= inv;
    }

    // Extract filename
    auto pos = path.find_last_of("/\\");
    slot.name = (pos != std::string::npos) ? path.substr(pos + 1) : path;

    std::fprintf(stderr, "[IR]   [%zu] %s (%zu samples)\n",
                 irs_.size(), slot.name.c_str(), slot.data.size());
    return true;
}

bool IrConvolver::load_wav(const std::string& path) {
    IrSlot slot;
    if (!load_wav_internal(path, slot)) {
        std::fprintf(stderr, "[IR] cannot load: %s\n", path.c_str());
        return false;
    }
    irs_.push_back(std::move(slot));
    resize_ring();
    return true;
}

int IrConvolver::load_dir(const std::string& dir_path) {
    std::vector<std::string> paths;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav")
                paths.push_back(entry.path().string());
        }
    }
    std::sort(paths.begin(), paths.end());

    int count = 0;
    for (const auto& p : paths) {
        IrSlot slot;
        if (load_wav_internal(p, slot)) {
            irs_.push_back(std::move(slot));
            ++count;
        }
    }
    if (count > 0) resize_ring();
    return count;
}

void IrConvolver::resize_ring() {
    // Find max IR length across all slots
    int max_len = 0;
    for (const auto& slot : irs_)
        max_len = std::max(max_len, static_cast<int>(slot.data.size()));

    // Round up to power of 2 for bitmask modulo (#51)
    ring_size_ = 1;
    while (ring_size_ < max_len) ring_size_ <<= 1;
    ring_mask_ = ring_size_ - 1;

    for (auto& ch : ch_) {
        ch.ring.assign(ring_size_, 0.0f);
        ch.write_pos = 0;
    }
}

bool IrConvolver::initialize_from_env() {
    if (const char* w = std::getenv("ELEPIANO_IR_WET"))
        wet_ = std::clamp(static_cast<float>(std::atof(w)), 0.0f, 1.0f);

    // Prefer directory over single file
    const char* dir = std::getenv("ELEPIANO_IR_DIR");
    if (dir && *dir) {
        int n = load_dir(dir);
        if (n > 0) {
            std::fprintf(stderr, "[IR] loaded %d IRs from %s\n", n, dir);
            return true;
        }
    }

    const char* path = std::getenv("ELEPIANO_IR_FILE");
    if (path && *path)
        return load_wav(path);

    return false;
}

void IrConvolver::set_wet(float w) {
    wet_ = std::clamp(w, 0.0f, 1.0f);
}

void IrConvolver::select(int index) {
    if (irs_.empty()) return;
    int new_idx = std::clamp(index, 0, static_cast<int>(irs_.size()) - 1);
    if (new_idx != active_) {
        active_ = new_idx;
        rt_log(RtLogEntry::Tag::IR_SELECT, active_);
    }
}

void IrConvolver::process(float* interleaved_stereo, int frames) {
    if (irs_.empty() || frames <= 0 || wet_ < 1e-6f) return;

    const auto& slot = irs_[active_];
    const int ir_len = static_cast<int>(slot.data.size());
    const float* ir = slot.data.data();
    const float wet = wet_;
    const float dry = 1.0f - wet;

    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float x = interleaved_stereo[i * 2 + ch];
            auto& state = ch_[ch];

            state.ring[state.write_pos] = x;

            float conv = 0.0f;
            int pos = state.write_pos;
            for (int k = 0; k < ir_len; ++k) {
                conv += ir[k] * state.ring[pos];
                pos = (pos - 1) & ring_mask_;
            }

            state.write_pos = (state.write_pos + 1) & ring_mask_;

            interleaved_stereo[i * 2 + ch] = x * dry + conv * wet;
        }
    }
}
