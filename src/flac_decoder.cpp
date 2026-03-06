#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_CRC
#include "dr_flac.h"

#include "flac_decoder.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

// ─── FLAC CRC ────────────────────────────────────────────────────
static uint8_t flac_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

static uint16_t flac_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
    }
    return crc;
}

// FLAC フレームヘッダの長さを返す（CRC-8 バイト含む）。不正なら -1
static int flac_header_len(const uint8_t* p, size_t avail)
{
    if (avail < 6) return -1;

    int len = 4;  // sync(2) + byte2 + byte3

    // UTF-8 coded frame/sample number
    uint8_t first = p[4];
    int utf8_len;
    if      ((first & 0x80) == 0)    utf8_len = 1;
    else if ((first & 0xE0) == 0xC0) utf8_len = 2;
    else if ((first & 0xF0) == 0xE0) utf8_len = 3;
    else if ((first & 0xF8) == 0xF0) utf8_len = 4;
    else if ((first & 0xFC) == 0xF8) utf8_len = 5;
    else if ((first & 0xFE) == 0xFC) utf8_len = 6;
    else if (first == 0xFE)          utf8_len = 7;
    else return -1;

    len += utf8_len;

    int bs_code = (p[2] >> 4) & 0x0F;
    if (bs_code == 6) len += 1;
    else if (bs_code == 7) len += 2;

    int sr_code = p[2] & 0x0F;
    if (sr_code == 12) len += 1;
    else if (sr_code == 13 || sr_code == 14) len += 2;

    len += 1;  // CRC-8

    if (static_cast<size_t>(len) > avail) return -1;
    return len;
}

static constexpr std::streamoff MAX_FILE_SIZE = 64 * 1024 * 1024; // 64 MB

static std::vector<uint8_t> read_file_bytes(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
        throw std::runtime_error("ファイルが開けません: " + path);
    auto size = ifs.tellg();
    if (size < 0)
        throw std::runtime_error("ファイルサイズ取得失敗: " + path);
    if (size > MAX_FILE_SIZE)
        throw std::runtime_error("ファイルサイズ超過: " + path);
    ifs.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    if (!ifs.good())
        throw std::runtime_error("ファイル読み込み失敗: " + path);
    return buf;
}

// FLAC メタデータブロックを読み飛ばしてオーディオデータ開始位置を返す
// block_size: STREAMINFO から読んだブロックサイズ
static size_t find_audio_start(const std::vector<uint8_t>& buf, int& block_size)
{
    block_size = 0;
    if (buf.size() < 8 || buf[0] != 'f' || buf[1] != 'L' ||
        buf[2] != 'a' || buf[3] != 'C')
        return 0;

    size_t pos = 4;
    while (pos + 4 <= buf.size()) {
        int btype = buf[pos] & 0x7F;
        bool is_last = (buf[pos] & 0x80) != 0;
        size_t blen = (static_cast<size_t>(buf[pos+1]) << 16) |
                      (static_cast<size_t>(buf[pos+2]) << 8)  |
                       static_cast<size_t>(buf[pos+3]);

        if (btype == 0 && blen >= 18) {
            size_t si = pos + 4;
            block_size = (static_cast<int>(buf[si+2]) << 8) |
                          static_cast<int>(buf[si+3]);  // max block size
        }

        pos += 4 + blen;
        if (is_last) break;
    }
    return pos;
}

// FLAC フレーム境界を検出（CRC-8 ヘッダ検証付き）
static std::vector<size_t> find_frame_starts(const std::vector<uint8_t>& buf, size_t audio_start)
{
    std::vector<size_t> starts;
    for (size_t pos = audio_start; pos + 6 < buf.size(); ++pos) {
        if (buf[pos] != 0xFF || (buf[pos + 1] & 0xFE) != 0xF8)
            continue;
        uint8_t b2 = buf[pos + 2];
        uint8_t b3 = buf[pos + 3];
        int bs_code = (b2 >> 4) & 0x0F;
        int sr_code = b2 & 0x0F;
        int reserved = b3 & 0x01;
        if (bs_code < 1 || sr_code < 1 || sr_code == 15 || reserved != 0)
            continue;

        // CRC-8 ヘッダ検証で偽陽性を排除
        int hdr_len = flac_header_len(&buf[pos], buf.size() - pos);
        if (hdr_len < 0) continue;

        uint8_t computed = flac_crc8(&buf[pos], hdr_len - 1);
        if (computed != buf[pos + hdr_len - 1]) continue;

        starts.push_back(pos);
    }
    return starts;
}

// 不良フレームの PCM 範囲をゼロ埋め + 境界フェード
static void mute_bad_frames(std::vector<float>& pcm, int block_size, int channels,
                             const std::vector<uint8_t>& buf,
                             const std::vector<size_t>& frame_starts)
{
    if (frame_starts.empty() || block_size <= 0 || channels <= 0) return;

    const size_t total_samples = pcm.size();
    const size_t frame_pcm_len = static_cast<size_t>(block_size) * static_cast<size_t>(channels);
    const int fade_len = 64;
    int muted = 0;

    for (size_t i = 0; i < frame_starts.size(); ++i) {
        size_t fpos = frame_starts[i];
        size_t next = (i + 1 < frame_starts.size()) ? frame_starts[i + 1] : buf.size();
        size_t frame_bytes = next - fpos;
        if (frame_bytes < 6) continue;

        // CRC-16 検証（フレーム末尾2バイトが CRC-16）
        uint16_t computed = flac_crc16(&buf[fpos], frame_bytes - 2);
        uint16_t stored = (static_cast<uint16_t>(buf[next - 2]) << 8) | buf[next - 1];
        if (computed == stored) continue;  // CRC OK

        // CRC NG → 該当フレームの PCM をゼロ埋め
        size_t sample_start = i * frame_pcm_len;
        size_t sample_end = sample_start + frame_pcm_len;
        if (sample_start >= total_samples) continue;
        if (sample_end > total_samples) sample_end = total_samples;

        std::memset(&pcm[sample_start], 0, (sample_end - sample_start) * sizeof(float));

        // 前後の境界にフェードを適用してクリック防止
        if (sample_start > 0) {
            size_t fade_start = (sample_start > static_cast<size_t>(fade_len))
                                ? sample_start - fade_len : 0;
            for (size_t s = fade_start; s < sample_start && s < total_samples; ++s) {
                float t = static_cast<float>(s - fade_start) / fade_len;
                pcm[s] *= (1.0f - t);
            }
        }
        if (sample_end < total_samples) {
            size_t fade_end = sample_end + fade_len;
            if (fade_end > total_samples) fade_end = total_samples;
            for (size_t s = sample_end; s < fade_end; ++s) {
                float t = static_cast<float>(s - sample_end) / fade_len;
                pcm[s] *= t;
            }
        }
        ++muted;
    }

    (void)muted;  // ログは decode_flac_file 側でファイル名付きで出力
}

// ─── メインデコード関数 ────────────────────────────────────────
DecodedAudio decode_flac_file(const std::string& path, size_t max_frames)
{
    // ファイルを1回だけ読み込み、メモリからデコード + CRC検証
    auto buf = read_file_bytes(path);

    auto closer = [](drflac* p) { if (p) drflac_close(p); };
    std::unique_ptr<drflac, decltype(closer)> f(
        drflac_open_memory(buf.data(), buf.size(), nullptr), closer);
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
    drflac_uint64 decoded = drflac_read_pcm_frames_f32(f.get(), frames_to_read, result.pcm.data());
    result.pcm.resize(static_cast<size_t>(decoded) * channels);

    if (result.pcm.empty())
        throw std::runtime_error("FLAC デコード結果が空: " + path);

    // CRC-16 検証: 不良フレームをミュートしてデジタルノイズを除去
    int block_size = 0;
    size_t audio_start = find_audio_start(buf, block_size);
    if (audio_start > 0 && block_size > 0) {
        auto frame_starts = find_frame_starts(buf, audio_start);
        int before_muted = 0;
        // mute前のCRCエラー数をカウント（ログ用）
        for (size_t i = 0; i < frame_starts.size(); ++i) {
            size_t fpos = frame_starts[i];
            size_t next = (i + 1 < frame_starts.size()) ? frame_starts[i + 1] : buf.size();
            size_t flen = next - fpos;
            if (flen < 6) continue;
            uint16_t computed = flac_crc16(&buf[fpos], flen - 2);
            uint16_t stored = (static_cast<uint16_t>(buf[next - 2]) << 8) | buf[next - 1];
            if (computed != stored) ++before_muted;
        }
        if (before_muted > 0)
            fprintf(stderr, "[FLAC] %s: %d/%zu frames CRC-16 NG\n",
                    path.c_str(), before_muted, frame_starts.size());
        mute_bad_frames(result.pcm, block_size, result.num_channels, buf, frame_starts);
    }

    return result;
}
