#pragma once
#include "voice.hpp"
#include "sample_db.hpp"
#include <array>
#include <memory>

class SynthEngine {
public:
    static constexpr int MAX_VOICES = 16;

    explicit SynthEngine(SampleDB& db, int sample_rate = 44100);

    void note_on(int midi_note, int velocity);
    void note_off(int midi_note);

    // buf: ステレオ float, frames フレーム分を合算
    void mix(float* buf, int frames);

private:
    SampleDB& db_;
    int       sample_rate_;
    std::array<Voice, MAX_VOICES> voices_;

    // 最古のアクティブ Voice インデックスを返す（全 Voice が IDLE なら -1）
    int oldest_voice_idx() const;
};
