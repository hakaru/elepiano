#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "flac_decoder.hpp"
#include <stdexcept>
#include <cstdio>

// ─── メインデコード関数 ────────────────────────────────────────
// dr_flac の内蔵 CRC-8/CRC-16 検証により、破損フレームは自動スキップされる
DecodedAudio decode_flac_file(const std::string& path, size_t max_frames)
{
    drflac* f = drflac_open_file(path.c_str(), nullptr);
    if (!f)
        throw std::runtime_error("FLAC open 失敗: " + path);

    DecodedAudio result;
    result.sample_rate     = static_cast<int>(f->sampleRate);
    result.num_channels    = static_cast<int>(f->channels);
    result.bits_per_sample = static_cast<int>(f->bitsPerSample);

    const size_t channels = static_cast<size_t>(f->channels);
    drflac_uint64 frames_to_read = f->totalPCMFrameCount;
    if (frames_to_read == 0)
        frames_to_read = max_frames > 0 ? max_frames : 441000;
    else if (max_frames > 0 && frames_to_read > max_frames)
        frames_to_read = max_frames;

    result.pcm.resize(static_cast<size_t>(frames_to_read) * channels);
    drflac_uint64 decoded = drflac_read_pcm_frames_f32(f, frames_to_read, result.pcm.data());
    result.pcm.resize(static_cast<size_t>(decoded) * channels);
    drflac_close(f);

    if (result.pcm.empty())
        throw std::runtime_error("FLAC デコード結果が空: " + path);

    return result;
}
