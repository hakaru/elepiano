#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_CRC
#include "dr_flac.h"

#include "flac_decoder.hpp"
#include <stdexcept>
#include <cstdio>
#include <fstream>

// ─── SpCA XOR 復号 ─────────────────────────────────────────────
// SpCA 由来の FLAC ファイルではフレーム1以降が
// XOR キー [0xEF, 0x42, 0x12, 0xBC] の4通りローテーションで暗号化されている。
// フレーム0 は平文なので、そのまま読むと最初の4096サンプルしかデコードできない。
static constexpr uint8_t XOR_KEY[4] = {0xEF, 0x42, 0x12, 0xBC};

// 暗号化された FLAC フレーム sync (0xFF 0xF8) を探す
static bool try_find_encrypted_sync(const std::vector<uint8_t>& buf,
                                     size_t search_start, size_t search_end,
                                     size_t& out_pos, int& out_rot)
{
    if (search_end > buf.size() - 4)
        search_end = buf.size() - 4;

    for (size_t pos = search_start; pos < search_end; ++pos) {
        for (int rot = 0; rot < 4; ++rot) {
            uint8_t b0 = buf[pos]     ^ XOR_KEY[rot];
            uint8_t b1 = buf[pos + 1] ^ XOR_KEY[(rot + 1) & 3];
            if (b0 != 0xFF || (b1 & 0xFE) != 0xF8)
                continue;

            // ヘッダー妥当性チェック
            uint8_t b2 = buf[pos + 2] ^ XOR_KEY[(rot + 2) & 3];
            int bs_code = (b2 >> 4) & 0x0F;
            int sr_code = b2 & 0x0F;

            // blocksize=4096(0xC) or similar, sample_rate=44100(0x9)
            if (bs_code >= 1 && sr_code == 9) {
                out_pos = pos;
                out_rot = rot;
                return true;
            }
        }
    }
    return false;
}

// XOR 復号を適用（バッファ内インプレース）
static void apply_xor(std::vector<uint8_t>& buf, size_t start, int rot)
{
    for (size_t i = start; i < buf.size(); ++i)
        buf[i] ^= XOR_KEY[(rot + (i - start)) & 3];
}

static constexpr std::streamoff MAX_FILE_SIZE = 64 * 1024 * 1024; // 64 MB

// ファイル全体を読み込む
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
static size_t find_audio_start(const std::vector<uint8_t>& buf)
{
    if (buf.size() < 8 || buf[0] != 'f' || buf[1] != 'L' ||
        buf[2] != 'a' || buf[3] != 'C')
        return 0;

    size_t pos = 4;
    while (pos + 4 <= buf.size()) {
        bool is_last = (buf[pos] & 0x80) != 0;
        size_t blen = (static_cast<size_t>(buf[pos+1]) << 16) |
                      (static_cast<size_t>(buf[pos+2]) << 8)  |
                       static_cast<size_t>(buf[pos+3]);
        pos += 4 + blen;
        if (is_last) break;
    }
    return pos;
}

// ─── メインデコード関数 ────────────────────────────────────────
DecodedAudio decode_flac_file(const std::string& path, size_t max_frames)
{
    // ファイルをメモリに読み込み
    auto buf = read_file_bytes(path);

    // まず平文として試行
    drflac* f = drflac_open_memory(buf.data(), buf.size(), nullptr);
    if (!f)
        throw std::runtime_error("FLAC open 失敗: " + path);

    DecodedAudio result;
    result.sample_rate     = static_cast<int>(f->sampleRate);
    result.num_channels    = static_cast<int>(f->channels);
    result.bits_per_sample = static_cast<int>(f->bitsPerSample);

    drflac_uint64 frames_to_read = f->totalPCMFrameCount;
    if (max_frames > 0 && frames_to_read > max_frames)
        frames_to_read = max_frames;

    result.pcm.resize(static_cast<size_t>(frames_to_read));
    drflac_uint64 decoded = drflac_read_pcm_frames_f32(f, frames_to_read, result.pcm.data());
    result.pcm.resize(static_cast<size_t>(decoded));
    drflac_close(f);

    // 十分にデコードできた → そのまま返す
    if (decoded > 8192) {
        if (result.pcm.empty())
            throw std::runtime_error("FLAC デコード結果が空: " + path);
        return result;
    }

    // フレーム0 のみ（≤4096 サンプル）→ SpCA XOR 暗号化の可能性
    size_t audio_start = find_audio_start(buf);
    if (audio_start == 0) {
        if (result.pcm.empty())
            throw std::runtime_error("FLAC デコード結果が空: " + path);
        return result;
    }

    // 暗号化フレームを探す（audio_start + 100 〜 +200KB の範囲）
    size_t enc_pos = 0;
    int enc_rot = 0;
    if (!try_find_encrypted_sync(buf, audio_start + 100,
                                  audio_start + 200000, enc_pos, enc_rot)) {
        // 暗号化されていない（短いサンプル）
        if (result.pcm.empty())
            throw std::runtime_error("FLAC デコード結果が空: " + path);
        return result;
    }

    // XOR 復号してリトライ
    drflac_uint64 plain_decoded = decoded;
    apply_xor(buf, enc_pos, enc_rot);

    f = drflac_open_memory(buf.data(), buf.size(), nullptr);
    if (f) {
        frames_to_read = f->totalPCMFrameCount;
        if (max_frames > 0 && frames_to_read > max_frames)
            frames_to_read = max_frames;

        std::vector<float> xor_pcm(static_cast<size_t>(frames_to_read));
        drflac_uint64 xor_decoded = drflac_read_pcm_frames_f32(f, frames_to_read, xor_pcm.data());
        drflac_close(f);

        // XOR 復号で改善した場合のみ採用
        if (xor_decoded > plain_decoded) {
            xor_pcm.resize(static_cast<size_t>(xor_decoded));
            result.pcm = std::move(xor_pcm);
        }
    }

    if (result.pcm.empty())
        throw std::runtime_error("FLAC デコード結果が空: " + path);

    return result;
}
