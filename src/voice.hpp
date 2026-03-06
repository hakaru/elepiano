#pragma once
#include "sample_db.hpp"
#include <cstdint>

struct Voice {
    enum class State { IDLE, PLAYING, RELEASING };

    const SampleData* sample    = nullptr;
    int   target_note           = 0;
    int   velocity              = 0;     // note_on 時の velocity（release voice 選択に使用）
    bool  is_release_voice      = false; // true の場合 note_off() は no-op（自然減衰で終了）
    bool  sustained             = false; // サステインペダルによるリリース保留
    float pitch_ratio           = 1.0f;
    double position             = 0.0;   // サンプル内浮動小数点位置
    float gain                  = 0.0f;  // 速度由来のゲイン
    float release_gain          = 1.0f;  // リリース中のゲインスケール
    float release_rate          = 0.0f;  // 1フレームあたりの減衰量
    State state                 = State::IDLE;
    uint64_t start_time_samples = 0;     // Voice スチール判定用（音符開始時の sample_counter_）

    void note_on(const SampleData* sd, int note, int velocity, int sample_rate,
                 float release_time_s = 0.200f);
    void note_off();

    // buf に frames 分のサンプルを加算する。IDLE になったら true を返す
    bool mix(float* buf, int frames);
};
