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
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>

static size_t get_max_sample_frames() {
    const char* env = std::getenv("ELEPIANO_MAX_SAMPLE_FRAMES");
    if (env && *env) {
        long val = std::atol(env);
        if (val >= 44100 && val <= 2646000) return static_cast<size_t>(val);
    }
    return 882000;  // default: 20秒 @ 44100Hz
}
static constexpr size_t FADE_OUT_FRAMES   = 4410;
static constexpr size_t MAX_JSON_SIZE     = 10 * 1024 * 1024;
static constexpr size_t MAX_SAMPLE_COUNT  = 10000;

// キャッシュファイルのマジックとバージョン
static constexpr uint32_t CACHE_MAGIC   = 0x50434D43; // "PCMC"
static constexpr uint32_t CACHE_VERSION = 9;  // v9: int16 in-memory PCM (halved RAM usage)

using json = nlohmann::json;
namespace fs = std::filesystem;

// 1サンプルの FLAC デコード + 後処理（スレッドセーフ）
static bool process_sample(SampleData& sd)
{
    DecodedAudio audio;
    try {
        audio = decode_flac_file(sd.file_path, get_max_sample_frames());
    } catch (const std::exception& e) {
        fprintf(stderr, "[SampleDB] SKIP %s: %s\n", sd.file_path.c_str(), e.what());
        return false;
    }

    if (audio.pcm.empty() || audio.pcm.size() < 44) {
        fprintf(stderr, "[SampleDB] SKIP %s: デコード結果が短い (%zu)\n",
                sd.file_path.c_str(), audio.pcm.size());
        return false;
    }

    {
        bool has_abnormal = false;
        for (float v : audio.pcm) {
            if (v < -2.0f || v > 2.0f) { has_abnormal = true; break; }
        }
        if (has_abnormal) {
            fprintf(stderr, "[SampleDB] SKIP %s: 異常な PCM 値\n", sd.file_path.c_str());
            return false;
        }
    }

    {
        float first = audio.pcm[0];
        bool all_same = true;
        for (float v : audio.pcm) {
            if (std::fabs(v - first) > 1e-6f) { all_same = false; break; }
        }
        if (all_same) {
            fprintf(stderr, "[SampleDB] SKIP %s: 全サンプルが同一値\n", sd.file_path.c_str());
            return false;
        }
    }

    // ステレオ → モノ
    if (audio.num_channels >= 2) {
        size_t ch = static_cast<size_t>(audio.num_channels);
        size_t total_frames = audio.pcm.size() / ch;
        std::vector<float> mono(total_frames);
        for (size_t f = 0; f < total_frames; ++f) {
            float sum = 0.0f;
            for (size_t c = 0; c < ch; ++c)
                sum += audio.pcm[f * ch + c];
            mono[f] = sum / static_cast<float>(ch);
        }
        audio.pcm = std::move(mono);
        audio.num_channels = 1;
    }

    // フェードアウト
    if (audio.pcm.size() >= FADE_OUT_FRAMES) {
        size_t fade_start = audio.pcm.size() - FADE_OUT_FRAMES;
        for (size_t i = 0; i < FADE_OUT_FRAMES; ++i) {
            float fade = 1.0f - static_cast<float>(i) / FADE_OUT_FRAMES;
            audio.pcm[fade_start + i] *= fade;
        }
    }

    // 先頭無音トリミング
    {
        size_t onset = 0;
        for (size_t i = 0; i < audio.pcm.size(); ++i) {
            if (std::fabs(audio.pcm[i]) > 1e-6f) { onset = i; break; }
        }
        if (onset > 0) {
            audio.pcm.erase(audio.pcm.begin(),
                            audio.pcm.begin() + static_cast<ptrdiff_t>(onset));
        }
    }

    // float → int16 変換してメモリ節約
    sd.pcm.resize(audio.pcm.size());
    for (size_t k = 0; k < audio.pcm.size(); ++k) {
        float v = audio.pcm[k] * 32767.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        sd.pcm[k] = static_cast<int16_t>(v);
    }
    sd.num_channels = audio.num_channels;
    sd.sample_rate  = audio.sample_rate;
    return true;
}

// ============================================================
// PCM キャッシュ: デコード済み PCM を int16 バイナリで保存/読込
// フォーマット (v6):
//   [magic:4][version:4][count:4][sample_rate:4]
//   per sample: [midi_note:4][vel_min:4][vel_max:4][vel_idx:4][rr:4][sr:4][ch:4][pcm_len:4][pcm:pcm_len*2 (int16)]
// ============================================================

static std::string cache_path_for(const std::string& json_path)
{
    return fs::path(json_path).parent_path() / ".pcm_cache";
}

static bool load_cache(const std::string& path, std::vector<SampleData>& out, int& sample_rate)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t magic, version, count, sr;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&count), 4);
    f.read(reinterpret_cast<char*>(&sr), 4);
    if (!f || magic != CACHE_MAGIC || version != CACHE_VERSION || count > MAX_SAMPLE_COUNT)
        return false;

    sample_rate = static_cast<int>(sr);
    out.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        SampleData sd;
        int32_t vals[8];
        f.read(reinterpret_cast<char*>(vals), sizeof(vals));
        if (!f) return false;

        sd.midi_note    = vals[0];
        sd.velocity_min = vals[1];
        sd.velocity_max = vals[2];
        sd.velocity_idx = vals[3];
        sd.round_robin  = vals[4];
        sd.sample_rate  = vals[5];
        sd.num_channels = vals[6];
        uint32_t pcm_len = static_cast<uint32_t>(vals[7]);

        if (pcm_len > get_max_sample_frames() * 2) return false;

        // int16 のまま直接読み込み（メモリ内も int16 で保持）
        sd.pcm.resize(pcm_len);
        f.read(reinterpret_cast<char*>(sd.pcm.data()), pcm_len * sizeof(int16_t));
        if (!f) return false;

        out.push_back(std::move(sd));
    }

    return true;
}

static void save_cache(const std::string& path, const std::vector<SampleData>& samples, int sample_rate)
{
    std::string tmp = path + ".tmp";
    std::ofstream f(tmp, std::ios::binary);
    if (!f) {
        fprintf(stderr, "[SampleDB] キャッシュ書き込み失敗: %s\n", path.c_str());
        return;
    }

    uint32_t magic   = CACHE_MAGIC;
    uint32_t version = CACHE_VERSION;
    uint32_t count   = static_cast<uint32_t>(samples.size());
    uint32_t sr      = static_cast<uint32_t>(sample_rate);
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(&sr), 4);

    for (const auto& sd : samples) {
        int32_t vals[8] = {
            sd.midi_note, sd.velocity_min, sd.velocity_max, sd.velocity_idx,
            sd.round_robin, sd.sample_rate, sd.num_channels,
            static_cast<int32_t>(sd.pcm.size())
        };
        f.write(reinterpret_cast<const char*>(vals), sizeof(vals));
        // PCM は既に int16 なので直接書き込み
        f.write(reinterpret_cast<const char*>(sd.pcm.data()), sd.pcm.size() * sizeof(int16_t));
    }

    f.close();
    if (f) {
        fs::rename(tmp, path);
        fprintf(stderr, "[SampleDB] キャッシュ保存: %s (%u サンプル)\n", path.c_str(), count);
    } else {
        fs::remove(tmp);
    }
}

// ============================================================
// SampleDB 実装
// ============================================================
SampleDB::SampleDB(const std::string& json_path)
{
    auto t0 = std::chrono::steady_clock::now();

    std::string cache = cache_path_for(json_path);

    // キャッシュが samples.json より新しければ使う
    bool use_cache = false;
    if (fs::exists(cache) && fs::exists(json_path)) {
        auto cache_time = fs::last_write_time(cache);
        auto json_time  = fs::last_write_time(json_path);
        if (cache_time >= json_time) {
            use_cache = load_cache(cache, samples_, sample_rate_);
            if (use_cache) {
                auto t1 = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                fprintf(stderr, "[SampleDB] キャッシュから %zu サンプル読み込み完了 (%lldms)\n",
                        samples_.size(), static_cast<long long>(ms));
            }
        }
    }

    if (!use_cache) {
        // FLAC デコードパス
        std::ifstream f(json_path);
        if (!f) throw std::runtime_error("samples.json が開けません: " + json_path);

        {
            auto file_size = fs::file_size(json_path);
            if (file_size > MAX_JSON_SIZE)
                throw std::runtime_error("samples.json が大きすぎます: " + std::to_string(file_size) + " bytes");
        }

        json j;
        try {
            f >> j;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("JSON パースエラー: ") + e.what());
        }

        sample_rate_ = j.value("sample_rate", 44100);
        const fs::path base_dir = fs::path(json_path).parent_path();

        if (!j.contains("samples") || !j["samples"].is_array())
            throw std::runtime_error("samples.json: 'samples' 配列が見つかりません");
        if (j["samples"].size() > MAX_SAMPLE_COUNT)
            throw std::runtime_error("samples[] が多すぎます: " + std::to_string(j["samples"].size()) + " 件");

        // Phase 1: メタデータパース
        std::vector<SampleData> tasks;
        tasks.reserve(j["samples"].size());

        for (auto& item : j["samples"]) {
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
            tasks.push_back(std::move(sd));
        }

        // Phase 2: 並列 FLAC デコード
        const size_t n = tasks.size();
        std::vector<uint8_t> ok(n, 0);

        unsigned num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        if (const char* env = std::getenv("ELEPIANO_DECODE_THREADS")) {
            int val = std::atoi(env);
            if (val >= 1 && val <= 16) num_threads = static_cast<unsigned>(val);
        }

        std::atomic<size_t> next_idx{0};
        auto worker = [&]() {
            for (;;) {
                size_t i = next_idx.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;
                ok[i] = process_sample(tasks[i]);
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (unsigned t = 0; t < num_threads; ++t)
            threads.emplace_back(worker);
        for (auto& t : threads)
            t.join();

        // Phase 3: 収集
        samples_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (ok[i])
                samples_.push_back(std::move(tasks[i]));
        }

        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        fprintf(stderr, "[SampleDB] %zu サンプル読み込み完了 (%lldms)\n",
                samples_.size(), static_cast<long long>(ms));

        // キャッシュ保存
        save_cache(cache, samples_, sample_rate_);
    }

    // ノートインデックス構築
    for (size_t i = 0; i < samples_.size(); ++i) {
        note_index_[samples_[i].midi_note].push_back(i);
    }
    candidates_.reserve(16);
}

const SampleData* SampleDB::find(int midi_note, int velocity)
{
    if (note_index_.empty()) return nullptr;

    // 最近傍ノートを1つだけ選ぶ（等距離なら低い方を優先）
    auto it = note_index_.lower_bound(midi_note);
    decltype(it) best_it = note_index_.end();

    if (it != note_index_.end() && it != note_index_.begin()) {
        auto pit = std::prev(it);
        int dist_hi = it->first - midi_note;
        int dist_lo = midi_note - pit->first;
        best_it = (dist_lo <= dist_hi) ? pit : it;
    } else if (it != note_index_.end()) {
        best_it = it;
    } else {
        best_it = std::prev(it);
    }

    if (best_it == note_index_.end()) return nullptr;

    // 選ばれたノートの候補からベロシティマッチ
    const auto& indices = best_it->second;
    size_t chosen = indices[0];
    int best_vel_dist = INT_MAX;
    for (size_t idx : indices) {
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
