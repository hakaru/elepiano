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

// FLAC CRC-8 (polynomial x^8 + x^2 + x^1 + x^0)
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

// XOR 復号した FLAC フレームヘッダの CRC-8 を検証
// フレームヘッダ: sync(2) + byte2(1) + byte3(1) + UTF-8(1-7) + [blocksize] + [samplerate] + CRC-8(1)
static bool verify_header_crc8(const std::vector<uint8_t>& buf,
                                size_t pos, int rot, int bs_code, int sr_code)
{
    if (pos + 16 > buf.size()) return false;

    // XOR 復号しながらヘッダバイトを収集
    uint8_t hdr[16];
    for (int i = 0; i < 4; ++i)
        hdr[i] = buf[pos + i] ^ XOR_KEY[(rot + i) & 3];
    size_t hdr_len = 4;

    // UTF-8 coded frame/sample number（可変長）
    uint8_t lead = buf[pos + 4] ^ XOR_KEY[(rot + 4) & 3];
    hdr[hdr_len++] = lead;
    int extra = 0;
    if      ((lead & 0x80) == 0x00) extra = 0;
    else if ((lead & 0xE0) == 0xC0) extra = 1;
    else if ((lead & 0xF0) == 0xE0) extra = 2;
    else if ((lead & 0xF8) == 0xF0) extra = 3;
    else if ((lead & 0xFC) == 0xF8) extra = 4;
    else if ((lead & 0xFE) == 0xFC) extra = 5;
    else if (lead == 0xFE)          extra = 6;
    else return false;

    if (pos + hdr_len + extra + 3 > buf.size()) return false;
    for (int i = 0; i < extra; ++i) {
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        if ((hdr[hdr_len] & 0xC0) != 0x80) return false;  // UTF-8 continuation
        hdr_len++;
    }

    // オプショナルな blocksize（bs_code 6=8bit, 7=16bit）
    if (bs_code == 6) {
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
    } else if (bs_code == 7) {
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
    }
    // オプショナルな sample rate（sr_code 12=8bit, 13,14=16bit）
    if (sr_code == 12) {
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
    } else if (sr_code == 13 || sr_code == 14) {
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
        hdr[hdr_len] = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
        hdr_len++;
    }

    // CRC-8 バイト
    if (pos + hdr_len >= buf.size()) return false;
    uint8_t crc_byte = buf[pos + hdr_len] ^ XOR_KEY[(rot + hdr_len) & 3];
    return flac_crc8(hdr, hdr_len) == crc_byte;
}

// 暗号化された FLAC フレーム sync (0xFF 0xF8) を探す
// channels, bps: STREAMINFO から取得した値で偽陽性を排除
static bool try_find_encrypted_sync(const std::vector<uint8_t>& buf,
                                     size_t search_start, size_t search_end,
                                     int channels, int bps,
                                     size_t& out_pos, int& out_rot)
{
    if (buf.size() < 8) return false;
    if (search_end > buf.size() - 4)
        search_end = buf.size() - 4;

    // FLAC byte 3: [channel_assignment(4) | sample_size(3) | reserved(1)]
    // reserved bit は 0 でなければならない
    // channel_assignment: 0-7=独立N+1ch, 8=L/S, 9=R/S, 10=M/S, 11-15=reserved
    // sample_size: 0=STREAMINFO参照, 1=8bit, 2=12bit, 4=16bit, 5=20bit, 6=24bit
    int expected_size_code = 0;
    if (bps == 8)  expected_size_code = 1;
    if (bps == 12) expected_size_code = 2;
    if (bps == 16) expected_size_code = 4;
    if (bps == 20) expected_size_code = 5;
    if (bps == 24) expected_size_code = 6;

    for (size_t pos = search_start; pos < search_end; ++pos) {
        for (int rot = 0; rot < 4; ++rot) {
            uint8_t b0 = buf[pos]     ^ XOR_KEY[rot];
            uint8_t b1 = buf[pos + 1] ^ XOR_KEY[(rot + 1) & 3];
            if (b0 != 0xFF || (b1 & 0xFE) != 0xF8)
                continue;

            uint8_t b2 = buf[pos + 2] ^ XOR_KEY[(rot + 2) & 3];
            int bs_code = (b2 >> 4) & 0x0F;
            int sr_code = b2 & 0x0F;
            if (bs_code < 1 || sr_code < 1 || sr_code == 15)
                continue;

            uint8_t b3 = buf[pos + 3] ^ XOR_KEY[(rot + 3) & 3];
            int ch_assign  = (b3 >> 4) & 0x0F;
            int size_code  = (b3 >> 1) & 0x07;
            int reserved   = b3 & 0x01;

            // reserved bit は 0
            if (reserved != 0) continue;
            // channel assignment: 0-10 のみ有効
            if (ch_assign > 10) continue;
            // チャンネル数の整合性チェック
            if (ch_assign <= 7 && (ch_assign + 1) != channels) continue;
            if (ch_assign >= 8 && channels != 2) continue;
            // ビット深度の整合性（0=STREAMINFO参照 も許容）
            if (size_code != 0 && expected_size_code != 0 && size_code != expected_size_code)
                continue;

            // CRC-8 でフレームヘッダの正当性を確定
            if (!verify_header_crc8(buf, pos, rot, bs_code, sr_code))
                continue;

            out_pos = pos;
            out_rot = rot;
            return true;
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
// max_frame_size: STREAMINFO から読んだ最大フレームサイズ（バイト）
static size_t find_audio_start(const std::vector<uint8_t>& buf, size_t& max_frame_size)
{
    max_frame_size = 0;
    if (buf.size() < 8 || buf[0] != 'f' || buf[1] != 'L' ||
        buf[2] != 'a' || buf[3] != 'C')
        return 0;

    size_t pos = 4;
    while (pos + 4 <= buf.size()) {
        int block_type = buf[pos] & 0x7F;
        bool is_last = (buf[pos] & 0x80) != 0;
        size_t blen = (static_cast<size_t>(buf[pos+1]) << 16) |
                      (static_cast<size_t>(buf[pos+2]) << 8)  |
                       static_cast<size_t>(buf[pos+3]);

        // STREAMINFO (type 0): max_frame_size はオフセット 7-9 (3バイト BE)
        if (block_type == 0 && blen >= 18) {
            size_t si = pos + 4;
            max_frame_size = (static_cast<size_t>(buf[si+7]) << 16) |
                             (static_cast<size_t>(buf[si+8]) << 8)  |
                              static_cast<size_t>(buf[si+9]);
        }

        pos += 4 + blen;
        if (is_last) break;
    }
    return pos;
}

// ─── メインデコード関数 ────────────────────────────────────────
DecodedAudio decode_flac_file(const std::string& path, size_t max_frames)
{
    // ファストパス: ファイルから直接デコード（メモリコピー不要）
    drflac* f = drflac_open_file(path.c_str(), nullptr);
    if (!f)
        throw std::runtime_error("FLAC open 失敗: " + path);

    DecodedAudio result;
    result.sample_rate     = static_cast<int>(f->sampleRate);
    result.num_channels    = static_cast<int>(f->channels);
    result.bits_per_sample = static_cast<int>(f->bitsPerSample);

    const size_t channels = static_cast<size_t>(f->channels);
    const drflac_uint64 total_frames = f->totalPCMFrameCount;
    drflac_uint64 frames_to_read = total_frames;
    // totalPCMFrameCount=0 は「不明」を意味する（SpCA 抽出ファイルで頻出）
    if (frames_to_read == 0)
        frames_to_read = max_frames > 0 ? max_frames : 441000;
    else if (max_frames > 0 && frames_to_read > max_frames)
        frames_to_read = max_frames;

    result.pcm.resize(static_cast<size_t>(frames_to_read) * channels);
    drflac_uint64 decoded = drflac_read_pcm_frames_f32(f, frames_to_read, result.pcm.data());
    result.pcm.resize(static_cast<size_t>(decoded) * channels);
    drflac_close(f);

    // DR_FLAC_NO_CRC は暗号化フレームもデコード「成功」として返すため、
    // decoded == frames_to_read でも暗号化されている可能性がある。
    // 常にファイルを読み込んで暗号化チェックを行う。
    auto buf = read_file_bytes(path);
    size_t max_frame_size = 0;
    size_t audio_start = find_audio_start(buf, max_frame_size);
    if (audio_start == 0) {
        // FLAC ヘッダなし → 最初のデコード結果をそのまま返す
        if (result.pcm.empty())
            throw std::runtime_error("FLAC デコード結果が空: " + path);
        return result;
    }

    // フレーム0 の sync（平文 0xFFF8）を飛ばし、暗号化 sync を探索
    // CRC-8 検証で偽陽性を完全排除
    size_t search_begin = audio_start + 6;
    size_t enc_pos = 0;
    int enc_rot = 0;
    if (!try_find_encrypted_sync(buf, search_begin, buf.size(),
                                  static_cast<int>(channels), result.bits_per_sample,
                                  enc_pos, enc_rot)) {
        // 暗号化されていない → 最初のデコード結果を返す
        if (result.pcm.empty())
            throw std::runtime_error("FLAC デコード結果が空: " + path);
        return result;
    }

    // CRC-8 検証済みの暗号化 sync → SpCA 暗号化ファイル確定
    // XOR 復号して全フレームを再デコード
    apply_xor(buf, enc_pos, enc_rot);

    f = drflac_open_memory(buf.data(), buf.size(), nullptr);
    if (f) {
        result.sample_rate     = static_cast<int>(f->sampleRate);
        result.num_channels    = static_cast<int>(f->channels);
        result.bits_per_sample = static_cast<int>(f->bitsPerSample);

        const size_t xor_channels = static_cast<size_t>(f->channels);
        frames_to_read = f->totalPCMFrameCount;
        if (frames_to_read == 0)
            frames_to_read = max_frames > 0 ? max_frames : 441000;
        else if (max_frames > 0 && frames_to_read > max_frames)
            frames_to_read = max_frames;

        result.pcm.resize(static_cast<size_t>(frames_to_read) * xor_channels);
        drflac_uint64 xor_decoded = drflac_read_pcm_frames_f32(f, frames_to_read, result.pcm.data());
        result.pcm.resize(static_cast<size_t>(xor_decoded) * xor_channels);
        drflac_close(f);
    }

    if (result.pcm.empty())
        throw std::runtime_error("FLAC デコード結果が空: " + path);

    return result;
}
