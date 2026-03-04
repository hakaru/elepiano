#pragma once
#include <string>
#include <vector>

/// FLAC デコード結果
struct DecodedAudio {
    std::vector<float> pcm;           // 正規化済みモノ PCM (-1.0 〜 +1.0)
    int sample_rate     = 44100;
    int num_channels    = 1;
    int bits_per_sample = 24;
};

/// FLAC ファイルをデコードして DecodedAudio を返す。
/// max_frames > 0 の場合、最大フレーム数を制限する（メモリ節約）。
/// 失敗時は std::runtime_error を投げる。
DecodedAudio decode_flac_file(const std::string& path, size_t max_frames = 0);
