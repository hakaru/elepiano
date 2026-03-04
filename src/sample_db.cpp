#include "sample_db.hpp"
#include "flac_decoder.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <climits>
#include <cmath>
#include <cstdio>

// Pi5 (8GB RAM) で全サンプルをメモリに収めるため、最大長を制限する
// 10秒 @ 44100Hz = 441000 サンプル → 1615 ファイル × 441000 × 4B ≈ 2.7GB
static constexpr size_t MAX_SAMPLE_FRAMES = 441000;
static constexpr size_t FADE_OUT_FRAMES   = 4410;  // 100ms フェードアウト

using json = nlohmann::json;
namespace fs = std::filesystem;

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
        if (!item.contains("midi_note") || !item["midi_note"].is_number_integer())
            throw std::runtime_error("samples[]: 'midi_note' は整数が必須");
        if (!item.contains("file") || !item["file"].is_string())
            throw std::runtime_error("samples[]: 'file' は文字列が必須");

        SampleData sd;
        sd.midi_note    = item["midi_note"].get<int>();
        sd.velocity_min = item.value("velocity_min", 0);
        sd.velocity_max = item.value("velocity_max", 127);
        sd.velocity_idx = item.value("velocity_idx", 0);
        sd.round_robin  = item.value("round_robin", 1);

        // パストラバーサル防止: relative() が ".." 始まりなら拒否
        fs::path file_path = base_dir / item["file"].get<std::string>();
        {
            auto cf = fs::weakly_canonical(file_path);
            auto cb = fs::weakly_canonical(base_dir);
            auto rel = fs::relative(cf, cb);
            if (rel.empty() || *rel.begin() == "..")
                throw std::runtime_error("パストラバーサルを検出: " + cf.string());
        }
        sd.file_path   = file_path.string();
        sd.sample_rate = sample_rate_;

        auto audio     = decode_flac_file(sd.file_path, MAX_SAMPLE_FRAMES);

        // フェードアウト（クリック防止）
        if (audio.pcm.size() >= FADE_OUT_FRAMES) {
            size_t fade_start = audio.pcm.size() - FADE_OUT_FRAMES;
            for (size_t i = 0; i < FADE_OUT_FRAMES; ++i) {
                float fade = 1.0f - static_cast<float>(i) / FADE_OUT_FRAMES;
                audio.pcm[fade_start + i] *= fade;
            }
        }

        sd.pcm         = std::move(audio.pcm);
        sd.num_channels = audio.num_channels;
        sd.sample_rate  = audio.sample_rate;

        samples_.push_back(std::move(sd));
    }

    // ノートインデックス構築 O(n)
    for (size_t i = 0; i < samples_.size(); ++i) {
        note_index_[samples_[i].midi_note].push_back(i);
    }

    candidates_.reserve(16);
    fprintf(stderr, "[SampleDB] %zu サンプル読み込み完了\n", samples_.size());
}

const SampleData* SampleDB::find(int midi_note, int velocity)
{
    if (note_index_.empty()) return nullptr;

    // O(log n): lower_bound で最近傍ノートを特定
    auto it = note_index_.lower_bound(midi_note);

    int best_dist = INT_MAX;
    if (it != note_index_.end())
        best_dist = std::min(best_dist, it->first - midi_note);  // >= 0
    if (it != note_index_.begin()) {
        auto pit = std::prev(it);
        best_dist = std::min(best_dist, midi_note - pit->first); // > 0
    }

    // 最近傍ノートのサンプルを candidates_ に収集
    candidates_.clear();
    if (it != note_index_.end() && it->first - midi_note == best_dist)
        for (size_t idx : it->second) candidates_.push_back(idx);
    if (it != note_index_.begin()) {
        auto pit = std::prev(it);
        if (midi_note - pit->first == best_dist)
            for (size_t idx : pit->second) candidates_.push_back(idx);
    }

    if (candidates_.empty()) return nullptr;

    // velocity マッチング: 範囲内を優先、なければ最近傍距離
    size_t chosen = candidates_[0];
    int best_vel_dist = INT_MAX;
    for (size_t idx : candidates_) {
        const auto& s = samples_[idx];
        if (velocity >= s.velocity_min && velocity <= s.velocity_max) {
            chosen = idx;
            best_vel_dist = 0;
            break;
        }
        int d = (velocity < s.velocity_min) ? s.velocity_min - velocity
                                             : velocity - s.velocity_max;
        if (d < best_vel_dist) { best_vel_dist = d; chosen = idx; }
    }

    return &samples_[chosen];
}
