#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct SampleData {
    int         midi_note;
    int         velocity_min;
    int         velocity_max;
    int         velocity_idx;
    int         round_robin;
    std::string file_path;          // 絶対パス

    // RAM に展開した 24-bit サンプル (float 変換済み)
    std::vector<float> pcm;         // モノ, 正規化済み (-1.0 〜 +1.0)
    int sample_rate  = 44100;
    int num_channels = 1;
};

class SampleDB {
public:
    explicit SampleDB(const std::string& json_path);

    // 最近傍ノート・速度レイヤーを検索して SampleData* を返す
    // round_robin は内部で順次インクリメントされる
    const SampleData* find(int midi_note, int velocity);

    int sample_rate() const { return sample_rate_; }

private:
    void load_flac(SampleData& sd);

    std::vector<SampleData> samples_;
    int sample_rate_ = 44100;

    // ラウンドロビン状態: キー = (midi_note, vel_layer_idx)
    // 値 = 次に使うRRインデックス
    std::vector<int> rr_counters_;  // samples_ と同サイズ
};
