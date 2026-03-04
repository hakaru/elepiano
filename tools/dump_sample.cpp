// デコード済み FLAC サンプルを WAV に書き出す診断ツール
// Usage: ./dump_sample input.flac output.wav
#include "../src/flac_decoder.hpp"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static void write_wav(const char* path, const float* pcm, size_t num_samples, int sample_rate)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "WAV 書き込み失敗: %s\n", path); return; }

    uint32_t data_size = num_samples * 2;  // 16-bit mono
    uint32_t file_size = 36 + data_size;

    // WAV header
    fwrite("RIFF", 1, 4, fp);
    fwrite(&file_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    uint16_t audio_fmt = 1;  // PCM
    fwrite(&audio_fmt, 2, 1, fp);
    uint16_t ch = 1;
    fwrite(&ch, 2, 1, fp);
    uint32_t sr = sample_rate;
    fwrite(&sr, 4, 1, fp);
    uint32_t byte_rate = sample_rate * 2;
    fwrite(&byte_rate, 4, 1, fp);
    uint16_t block_align = 2;
    fwrite(&block_align, 2, 1, fp);
    uint16_t bits = 16;
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);

    // PCM data (float → S16)
    for (size_t i = 0; i < num_samples; ++i) {
        float s = pcm[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = static_cast<int16_t>(s * 32767.0f);
        fwrite(&v, 2, 1, fp);
    }
    fclose(fp);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.flac output.wav\n", argv[0]);
        return 1;
    }
    try {
        auto audio = decode_flac_file(argv[1]);
        fprintf(stderr, "sample_rate=%d channels=%d bps=%d samples=%zu\n",
                audio.sample_rate, audio.num_channels, audio.bits_per_sample,
                audio.pcm.size());
        write_wav(argv[2], audio.pcm.data(), audio.pcm.size(), audio.sample_rate);
        fprintf(stderr, "WAV written: %s\n", argv[2]);
    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}
