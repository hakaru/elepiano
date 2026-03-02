#include "synth_engine.hpp"
#include <cstring>
#include <climits>
#include <cstdio>

SynthEngine::SynthEngine(SampleDB& attack_db, int sample_rate, SampleDB* release_db)
    : db_(attack_db), release_db_(release_db), sample_rate_(sample_rate)
{}

void SynthEngine::push_event(const MidiEvent& ev)
{
    if (!event_queue_.push(ev) && ev.type == MidiEvent::Type::NOTE_OFF) {
        // NOTE_OFF ドロップはゾンビボイスを引き起こすため警告
        fprintf(stderr, "[SynthEngine] WARNING: queue full, NOTE_OFF dropped (ch=%d note=%d)\n",
                ev.channel, ev.note);
    }
}

void SynthEngine::mix(float* buf, int frames)
{
    std::memset(buf, 0, frames * 2 * sizeof(float));

    // キューを全消費（オーディオスレッドのみがここに来る）
    while (auto ev = event_queue_.pop()) {
        if (ev->type == MidiEvent::Type::NOTE_ON) {
            _note_on(ev->note, ev->velocity);
        } else if (ev->type == MidiEvent::Type::NOTE_OFF) {
            _note_off(ev->note);
        }
    }

    for (auto& v : voices_) {
        if (v.state != Voice::State::IDLE) {
            v.mix(buf, frames);
        }
    }

    sample_counter_ += static_cast<uint64_t>(frames);
}

void SynthEngine::_note_on(int midi_note, int velocity)
{
    if (velocity == 0) {
        _note_off(midi_note);
        return;
    }

    const SampleData* sd = db_.find(midi_note, velocity);
    if (!sd) return;

    // IDLE な Voice を探す
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.state == Voice::State::IDLE) { target = &v; break; }
    }

    // IDLE がなければ start_time_samples が最小の（最古の）PLAYING Voice を奪う
    if (!target) {
        int idx = oldest_voice_idx();
        if (idx >= 0) target = &voices_[idx];
    }

    if (!target) return;

    target->note_on(sd, midi_note, velocity, sample_rate_);
    target->start_time_samples = sample_counter_;
}

void SynthEngine::_note_off(int midi_note)
{
    for (auto& v : voices_) {
        if (v.state == Voice::State::PLAYING && v.target_note == midi_note
                && !v.is_release_voice) {
            if (release_db_) {
                _start_release_voice(midi_note, v.velocity);
            }
            v.note_off();
        }
    }
}

void SynthEngine::_start_release_voice(int midi_note, int velocity)
{
    const SampleData* sd = release_db_->find(midi_note, velocity);
    if (!sd) return;

    // IDLE な Voice を探す
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.state == Voice::State::IDLE) { target = &v; break; }
    }

    // IDLE がなければ最古の非 release voice を奪う
    if (!target) {
        int idx = oldest_voice_idx();
        if (idx >= 0) target = &voices_[idx];
    }

    if (!target) return;

    target->note_on(sd, midi_note, velocity, sample_rate_);
    target->is_release_voice  = true;
    target->start_time_samples = sample_counter_;
}

int SynthEngine::oldest_voice_idx() const
{
    int oldest = -1;
    uint64_t min_time = UINT64_MAX;

    // 非 release voice を優先スチール
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].state == Voice::State::PLAYING &&
            !voices_[i].is_release_voice &&
            voices_[i].start_time_samples < min_time) {
            min_time = voices_[i].start_time_samples;
            oldest = i;
        }
    }
    // 非 release がなければ release voice も対象
    if (oldest == -1) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (voices_[i].state == Voice::State::PLAYING &&
                voices_[i].start_time_samples < min_time) {
                min_time = voices_[i].start_time_samples;
                oldest = i;
            }
        }
    }
    return oldest;
}
