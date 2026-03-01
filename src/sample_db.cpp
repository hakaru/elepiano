#include "sample_db.hpp"
#include <nlohmann/json.hpp>
#include <FLAC/stream_decoder.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// FLAC デコードコールバック
// ============================================================
struct FlacCtx {
    std::vector<float>* pcm;
    int channels;
    int bits_per_sample;
    int sample_rate;
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
    const int n = frame->header.blocksize;

    // モノ: ch0 のみ使用
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
        ctx->channels       = meta->data.stream_info.channels;
        ctx->bits_per_sample = meta->data.stream_info.bits_per_sample;
        ctx->sample_rate    = meta->data.stream_info.sample_rate;
    }
}

static void flac_error_cb(
    const FLAC__StreamDecoder*,
    FLAC__StreamDecoderErrorStatus,
    void*)
{
    // エラーは無視（SpCA フレーム0のCRC不一致などを許容）
}

// ============================================================
// SampleDB 実装
// ============================================================
SampleDB::SampleDB(const std::string& json_path)
{
    std::ifstream f(json_path);
    if (!f) throw std::runtime_error("samples.json が開けません: " + json_path);

    json j;
    f >> j;

    sample_rate_ = j.value("sample_rate", 44100);
    const fs::path base_dir = fs::path(json_path).parent_path();

    for (auto& item : j["samples"]) {
        SampleData sd;
        sd.midi_note    = item["midi_note"];
        sd.velocity_min = item["velocity_min"];
        sd.velocity_max = item["velocity_max"];
        sd.velocity_idx = item["velocity_idx"];
        sd.round_robin  = item["round_robin"];
        sd.file_path    = (base_dir / item["file"].get<std::string>()).string();
        sd.sample_rate  = sample_rate_;

        load_flac(sd);
        samples_.push_back(std::move(sd));
    }

    rr_counters_.assign(samples_.size(), 0);
    printf("[SampleDB] %zu サンプル読み込み完了\n", samples_.size());
}

void SampleDB::load_flac(SampleData& sd)
{
    FlacCtx ctx;
    ctx.pcm          = &sd.pcm;
    ctx.channels     = 1;
    ctx.bits_per_sample = 24;
    ctx.sample_rate  = sd.sample_rate;
    ctx.file_path    = sd.file_path;

    FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
    if (!dec) throw std::runtime_error("FLAC__stream_decoder_new 失敗");

    // SpCA フレーム0の CRC エラーを許容
    FLAC__stream_decoder_set_md5_checking(dec, false);

    auto init_status = FLAC__stream_decoder_init_file(
        dec, sd.file_path.c_str(),
        flac_write_cb, flac_meta_cb, flac_error_cb,
        &ctx);
    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(dec);
        throw std::runtime_error("FLAC init 失敗: " + sd.file_path);
    }

    FLAC__stream_decoder_process_until_end_of_stream(dec);
    FLAC__stream_decoder_finish(dec);
    FLAC__stream_decoder_delete(dec);

    sd.num_channels = ctx.channels;
    sd.sample_rate  = ctx.sample_rate;
}

const SampleData* SampleDB::find(int midi_note, int velocity)
{
    if (samples_.empty()) return nullptr;

    // 1. ノートが一致するグループから最近傍速度レイヤーを選ぶ
    //    まず midi_note に最も近いサンプルを全て収集
    int best_note_dist = INT_MAX;
    for (auto& s : samples_) {
        int d = std::abs(s.midi_note - midi_note);
        if (d < best_note_dist) best_note_dist = d;
    }

    // 2. そのノート距離の中から velocity が範囲内のものを探す
    //    なければ最近傍速度で選ぶ
    std::vector<size_t> candidates;
    for (size_t i = 0; i < samples_.size(); ++i) {
        if (std::abs(samples_[i].midi_note - midi_note) == best_note_dist) {
            candidates.push_back(i);
        }
    }

    // 速度範囲でフィルタ
    size_t chosen = candidates[0];
    int best_vel_dist = INT_MAX;
    for (size_t idx : candidates) {
        const auto& s = samples_[idx];
        if (velocity >= s.velocity_min && velocity <= s.velocity_max) {
            // 完全一致
            chosen = idx;
            best_vel_dist = 0;
            break;
        }
        int d = std::min(std::abs(velocity - s.velocity_min),
                         std::abs(velocity - s.velocity_max));
        if (d < best_vel_dist) {
            best_vel_dist = d;
            chosen = idx;
        }
    }

    return &samples_[chosen];
}
