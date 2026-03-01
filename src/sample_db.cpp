#include "sample_db.hpp"
#include <nlohmann/json.hpp>
#include <FLAC/stream_decoder.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// FLAC デコードコールバック
// ============================================================
struct FlacCtx {
    std::vector<float>* pcm;
    int channels        = 1;
    int bits_per_sample = 24;
    int sample_rate     = 44100;
    std::string file_path;
};

static FLAC__StreamDecoderWriteStatus flac_write_cb(
    const FLAC__StreamDecoder*,
    const FLAC__Frame* frame,
    const FLAC__int32* const buffer[],
    void* client_data)
{
    auto* ctx = static_cast<FlacCtx*>(client_data);
    const float scale = 1.0f / static_cast<float>(1 << (ctx->bits_per_sample - 1));
    const int n = static_cast<int>(frame->header.blocksize);

    ctx->pcm->reserve(ctx->pcm->size() + n);
    for (int i = 0; i < n; ++i) {
        ctx->pcm->push_back(buffer[0][i] * scale);
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_meta_cb(
    const FLAC__StreamDecoder*,
    const FLAC__StreamMetadata* meta,
    void* client_data)
{
    auto* ctx = static_cast<FlacCtx*>(client_data);
    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->channels        = static_cast<int>(meta->data.stream_info.channels);
        ctx->bits_per_sample = static_cast<int>(meta->data.stream_info.bits_per_sample);
        ctx->sample_rate     = static_cast<int>(meta->data.stream_info.sample_rate);
    }
}

static void flac_error_cb(
    const FLAC__StreamDecoder*,
    FLAC__StreamDecoderErrorStatus status,
    void* client_data)
{
    // SpCA フレーム0 の CRC 不一致は想定内。それ以外は警告表示。
    if (status != FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC &&
        status != FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER) {
        auto* ctx = static_cast<FlacCtx*>(client_data);
        fprintf(stderr, "[SampleDB] FLAC error in %s: %d\n",
                ctx->file_path.c_str(), static_cast<int>(status));
    }
}

// ============================================================
// FLAC デコーダー RAII ラッパー
// ============================================================
struct FlacDecoderDeleter {
    void operator()(FLAC__StreamDecoder* d) const {
        if (d) {
            FLAC__stream_decoder_finish(d);
            FLAC__stream_decoder_delete(d);
        }
    }
};
using FlacDecoderPtr = std::unique_ptr<FLAC__StreamDecoder, FlacDecoderDeleter>;

// ============================================================
// SampleDB 実装
// ============================================================
SampleDB::SampleDB(const std::string& json_path)
{
    std::ifstream f(json_path);
    if (!f) throw std::runtime_error("samples.json が開けません: " + json_path);

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("JSON パースエラー: ") + e.what());
    }

    sample_rate_ = j.value("sample_rate", 44100);
    const fs::path base_dir = fs::path(json_path).parent_path();

    if (!j.contains("samples") || !j["samples"].is_array()) {
        throw std::runtime_error("samples.json: 'samples' 配列が見つかりません");
    }

    for (auto& item : j["samples"]) {
        // 必須フィールドの型チェック
        if (!item.contains("midi_note") || !item["midi_note"].is_number_integer()) {
            throw std::runtime_error("samples[]: 'midi_note' は整数が必須");
        }
        if (!item.contains("file") || !item["file"].is_string()) {
            throw std::runtime_error("samples[]: 'file' は文字列が必須");
        }

        SampleData sd;
        sd.midi_note    = item["midi_note"].get<int>();
        sd.velocity_min = item.value("velocity_min", 0);
        sd.velocity_max = item.value("velocity_max", 127);
        sd.velocity_idx = item.value("velocity_idx", 0);
        sd.round_robin  = item.value("round_robin", 1);

        // パストラバーサル防止
        fs::path file_path = base_dir / item["file"].get<std::string>();
        {
            auto canonical_file = fs::weakly_canonical(file_path);
            auto canonical_base = fs::weakly_canonical(base_dir);
            auto fs_str  = canonical_file.string();
            auto bs_str  = canonical_base.string();
            if (fs_str.compare(0, bs_str.size(), bs_str) != 0) {
                throw std::runtime_error(
                    "パストラバーサルを検出: " + fs_str);
            }
        }
        sd.file_path   = file_path.string();
        sd.sample_rate = sample_rate_;

        load_flac(sd);
        samples_.push_back(std::move(sd));
    }

    candidates_.reserve(16);
    fprintf(stderr, "[SampleDB] %zu サンプル読み込み完了\n", samples_.size());
}

void SampleDB::load_flac(SampleData& sd)
{
    FlacCtx ctx;
    ctx.pcm          = &sd.pcm;
    ctx.channels     = 1;
    ctx.bits_per_sample = 24;
    ctx.sample_rate  = sd.sample_rate;
    ctx.file_path    = sd.file_path;

    FlacDecoderPtr dec(FLAC__stream_decoder_new());
    if (!dec) throw std::runtime_error("FLAC__stream_decoder_new 失敗");

    FLAC__stream_decoder_set_md5_checking(dec.get(), false);

    auto init_status = FLAC__stream_decoder_init_file(
        dec.get(), sd.file_path.c_str(),
        flac_write_cb, flac_meta_cb, flac_error_cb,
        &ctx);
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        throw std::runtime_error("FLAC init 失敗: " + sd.file_path);
    }

    FLAC__stream_decoder_process_until_end_of_stream(dec.get());
    // dec は FlacDecoderDeleter により自動で finish + delete される

    if (sd.pcm.empty()) {
        throw std::runtime_error("FLAC デコード結果が空: " + sd.file_path);
    }

    sd.num_channels = ctx.channels;
    sd.sample_rate  = ctx.sample_rate;
}

const SampleData* SampleDB::find(int midi_note, int velocity)
{
    if (samples_.empty()) return nullptr;

    // 1. 最近傍ノート距離を求める
    int best_note_dist = INT_MAX;
    for (const auto& s : samples_) {
        int d = std::abs(s.midi_note - midi_note);
        if (d < best_note_dist) best_note_dist = d;
    }

    // 2. その距離のサンプルを候補として収集（メンババッファを再利用）
    candidates_.clear();
    for (size_t i = 0; i < samples_.size(); ++i) {
        if (std::abs(samples_[i].midi_note - midi_note) == best_note_dist) {
            candidates_.push_back(i);
        }
    }

    if (candidates_.empty()) return nullptr;  // 通常あり得ないが安全ガード

    // 3. velocity が範囲内のものを優先、なければ最近傍距離で選択
    size_t chosen = candidates_[0];
    int best_vel_dist = INT_MAX;
    for (size_t idx : candidates_) {
        const auto& s = samples_[idx];
        if (velocity >= s.velocity_min && velocity <= s.velocity_max) {
            chosen = idx;
            best_vel_dist = 0;
            break;
        }
        int d = (velocity < s.velocity_min) ? (s.velocity_min - velocity)
                                             : (velocity - s.velocity_max);
        if (d < best_vel_dist) {
            best_vel_dist = d;
            chosen = idx;
        }
    }

    return &samples_[chosen];
}
