#pragma once
#include "voice.hpp"
#include "sample_db.hpp"
#include "midi_input.hpp"
#include "spsc_queue.hpp"
#include <array>
#include <cstdint>

class SynthEngine {
public:
    static constexpr int MAX_VOICES = 16;

    explicit SynthEngine(SampleDB& db, int sample_rate = 44100);

    // MIDIスレッドから呼ぶ（ロックフリー push）
    // キューが満杯のイベントは無視される（実用上 64 要素で十分）
    void push_event(const MidiEvent& ev);

    // オーディオスレッドから呼ぶ
    // buf: ステレオ float (frames * 2 要素), frames フレーム分を合算
    void mix(float* buf, int frames);

private:
    // 以下はオーディオスレッドのみからアクセス（mutex 不要）
    void _note_on(int midi_note, int velocity);
    void _note_off(int midi_note);
    int  oldest_voice_idx() const;

    SampleDB& db_;
    int       sample_rate_;
    uint64_t  sample_counter_ = 0;           // mix() 呼び出し時に frames 分インクリメント

    std::array<Voice, MAX_VOICES> voices_;   // オーディオスレッドのみ
    SpscQueue<MidiEvent, 64>      event_queue_;  // MIDI → オーディオスレッド間
};
