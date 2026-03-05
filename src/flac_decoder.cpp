#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_CRC
#include "dr_flac.h"

#include "flac_decoder.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <cmath>

// ─── PCM 破損検出 ─────────────────────────────────────────────────
// DR_FLAC_NO_CRC で全フレームをデコードすると、破損フレームはゴミデータになる。
// 隣接サンプル間の大きなジャンプが多いブロックを破損と判定し、ゼロ埋め＋フェードで修復。
static void fix_corrupt_segments(std::vector<float>& pcm, int channels)
{
    const size_t total = pcm.size();
    if (total < 2) return;

    const size_t block = 4096 * static_cast<size_t>(std::max(channels, 1));
    const float threshold = 0.3f;
    const int max_jumps = 50;
    const int fade_len = 64;
    int muted = 0;

    for (size_t bs = 0; bs < total; bs += block) {
        size_t be = std::min(bs + block, total);

        int jumps = 0;
        for (size_t i = bs + 1; i < be; ++i) {
            if (std::fabs(pcm[i] - pcm[i - 1]) > threshold)
                if (++jumps >= max_jumps) break;
        }
        if (jumps < max_jumps) continue;

        // 破損ブロック → ゼロ埋め
        std::memset(&pcm[bs], 0, (be - bs) * sizeof(float));

        // 前後の境界にフェードを適用してクリック防止
        if (bs > 0) {
            size_t fs = (bs > static_cast<size_t>(fade_len)) ? bs - fade_len : 0;
            for (size_t s = fs; s < bs && s < total; ++s)
                pcm[s] *= 1.0f - static_cast<float>(s - fs) / static_cast<float>(fade_len);
        }
        if (be < total) {
            size_t fe = std::min(be + static_cast<size_t>(fade_len), total);
            for (size_t s = be; s < fe; ++s)
                pcm[s] *= static_cast<float>(s - be) / static_cast<float>(fade_len);
        }
        ++muted;
    }

    if (muted > 0)
        fprintf(stderr, "[FLAC] %d blocks muted (PCM anomaly)\n", muted);
}

// ─── メインデコード関数 ────────────────────────────────────────
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

    // PCM レベルで破損セグメントを検出・ミュート
    fix_corrupt_segments(result.pcm, result.num_channels);

    return result;
}
