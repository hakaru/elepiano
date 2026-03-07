#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <cstdint>

struct SampleData {
    int         midi_note;
    int         velocity_min;
    int         velocity_max;
    int         velocity_idx;
    int         round_robin;
    std::string file_path;

    // RAM に展開した PCM サンプル (int16 正規化済み, モノにダウンミックス済み)
    // メモリ節約のため int16 で保持し、再生時に float に変換する
    std::vector<int16_t> pcm;
    int sample_rate  = 44100;
    int num_channels = 1;
};

class SampleDB {
public:
    explicit SampleDB(const std::string& json_path);

    // 最近傍ノート・速度レイヤーを O(log n) で検索して SampleData* を返す。
    // 見つからない場合は nullptr。
    const SampleData* find(int midi_note, int velocity);

    int sample_rate() const { return sample_rate_; }

private:
    std::vector<SampleData>            samples_;
    std::map<int, std::vector<size_t>> note_index_;  // midi_note → samples_ インデックス群
    int                                sample_rate_ = 44100;

    // find() の内部バッファ（動的確保を避けるためメンバ変数化）
    std::vector<size_t> candidates_;
};
