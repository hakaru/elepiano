// SpCA 抽出 FLAC のサニタイズツール
// dr_flac デコード + CRC-16 ミュート + PCM バースト検出 → クリーン 24-bit WAV 出力
// CRC-16 で全フレームがゼロ化された場合は exit(2) で通知
// Usage: ./sanitize_flac input.flac output.wav [--stats]
#include "../src/flac_decoder.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// PCM バースト検出: スライディングウィンドウ中央値との比較で
// CRC-16 をすり抜けた破損フレームを検出・ミュート
static int detect_and_mute_bursts(std::vector<float>& pcm, int /*sample_rate*/)
{
    const int block_samples = 256;  // ~5.8ms at 44100Hz
    const int total = static_cast<int>(pcm.size());
    const int num_blocks = total / block_samples;
    if (num_blocks < 4) return 0;

    // ブロックごとの RMS 計算
    std::vector<float> rms(num_blocks);
    for (int b = 0; b < num_blocks; ++b) {
        double sum = 0.0;
        int start = b * block_samples;
        for (int i = 0; i < block_samples; ++i) {
            float s = pcm[start + i];
            sum += static_cast<double>(s) * s;
        }
        rms[b] = static_cast<float>(std::sqrt(sum / block_samples));
    }

    // スライディングウィンドウ中央値ベースのバースト検出
    const int half_window = 16;  // ±16 blocks = ±93ms
    const float burst_threshold = 5.0f;  // 中央値の 5 倍超 → バースト
    const float min_rms = 0.005f;  // 絶対値閾値（無音部分は無視）
    const int fade_len = 32;  // クロスフェード長

    std::vector<bool> is_burst(num_blocks, false);
    std::vector<float> window_buf;

    for (int b = 0; b < num_blocks; ++b) {
        int w_start = std::max(0, b - half_window);
        int w_end   = std::min(num_blocks, b + half_window + 1);

        window_buf.clear();
        for (int w = w_start; w < w_end; ++w)
            window_buf.push_back(rms[w]);
        std::sort(window_buf.begin(), window_buf.end());
        float median = window_buf[window_buf.size() / 2];

        if (rms[b] > min_rms && median > 0.0f && rms[b] > median * burst_threshold)
            is_burst[b] = true;
    }

    // バーストブロックをゼロ埋め + クロスフェード
    int muted = 0;
    for (int b = 0; b < num_blocks; ++b) {
        if (!is_burst[b]) continue;
        ++muted;

        int start = b * block_samples;
        int end   = start + block_samples;
        if (end > total) end = total;

        std::memset(&pcm[start], 0, (end - start) * sizeof(float));

        // 前方フェードアウト
        if (start > 0) {
            int fs = std::max(0, start - fade_len);
            for (int i = fs; i < start && i < total; ++i) {
                float t = static_cast<float>(start - i) / fade_len;
                pcm[i] *= t;
            }
        }
        // 後方フェードイン
        if (end < total) {
            int fe = std::min(total, end + fade_len);
            for (int i = end; i < fe; ++i) {
                float t = static_cast<float>(i - end) / fade_len;
                pcm[i] *= t;
            }
        }
    }

    return muted;
}

// 全サンプルがゼロ（または同一値）かチェック
static bool is_all_silent(const std::vector<float>& pcm)
{
    if (pcm.empty()) return true;
    float first = pcm[0];
    for (float v : pcm) {
        if (std::fabs(v - first) > 1e-6f) return false;
    }
    return true;
}

static void write_wav(const char* path, const float* pcm, size_t num_samples,
                      int sample_rate, int channels, int bits_per_sample)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "WAV 書き込み失敗: %s\n", path); return; }

    int bytes_per_sample = bits_per_sample / 8;
    uint32_t data_size = static_cast<uint32_t>(num_samples * bytes_per_sample);
    uint32_t file_size = 36 + data_size;
    uint16_t audio_fmt = 1;
    uint16_t ch = static_cast<uint16_t>(channels);
    uint32_t sr = static_cast<uint32_t>(sample_rate);
    uint32_t byte_rate = sr * ch * bytes_per_sample;
    uint16_t block_align = ch * bytes_per_sample;
    uint16_t bits = static_cast<uint16_t>(bits_per_sample);
    uint32_t fmt_size = 16;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&file_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&audio_fmt, 2, 1, fp);
    fwrite(&ch, 2, 1, fp);
    fwrite(&sr, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);

    for (size_t i = 0; i < num_samples; ++i) {
        float s = pcm[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        if (bits_per_sample == 24) {
            int32_t v = static_cast<int32_t>(s * 8388607.0f);
            uint8_t b[3] = {
                static_cast<uint8_t>(v & 0xFF),
                static_cast<uint8_t>((v >> 8) & 0xFF),
                static_cast<uint8_t>((v >> 16) & 0xFF)
            };
            fwrite(b, 1, 3, fp);
        } else {
            int16_t v = static_cast<int16_t>(s * 32767.0f);
            fwrite(&v, 2, 1, fp);
        }
    }
    fclose(fp);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.flac output.wav [--stats]\n", argv[0]);
        return 1;
    }

    bool show_stats = false;
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--stats") show_stats = true;
    }

    try {
        auto audio = decode_flac_file(argv[1]);

        if (show_stats) {
            fprintf(stderr, "  decode: rate=%d ch=%d bps=%d samples=%zu\n",
                    audio.sample_rate, audio.num_channels, audio.bits_per_sample,
                    audio.pcm.size());
        }

        // CRC-16 ミュートで全ゼロになった場合はスキップ（exit 2）
        if (is_all_silent(audio.pcm)) {
            fprintf(stderr, "  SILENT: all samples zero after CRC-16 muting\n");
            return 2;
        }

        // PCM バースト検出 & ミュート
        int bursts = detect_and_mute_bursts(audio.pcm, audio.sample_rate);

        if (bursts > 0 || show_stats) {
            fprintf(stderr, "  bursts: %d blocks muted (PCM anomaly)\n", bursts);
        }

        write_wav(argv[2], audio.pcm.data(), audio.pcm.size(),
                  audio.sample_rate, audio.num_channels, audio.bits_per_sample);

    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s: %s\n", argv[1], e.what());
        return 1;
    }
    return 0;
}
