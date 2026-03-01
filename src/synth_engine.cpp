#include "synth_engine.hpp"
#include <cstring>
#include <algorithm>

SynthEngine::SynthEngine(SampleDB& db, int sample_rate)
    : db_(db), sample_rate_(sample_rate)
{}

void SynthEngine::note_on(int midi_note, int velocity)
{
    if (velocity == 0) {
        note_off(midi_note);
        return;
    }

    const SampleData* sd = db_.find(midi_note, velocity);
    if (!sd) return;

    std::lock_guard<std::mutex> lock(voice_mutex_);

    // IDLE な Voice を探す
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.state == Voice::State::IDLE) {
            target = &v;
            break;
        }
    }

    // IDLE がなければ最古の PLAYING Voice を奪う
    if (!target) {
        int idx = oldest_voice_idx();
        if (idx >= 0) target = &voices_[idx];
    }

    if (!target) return;

    target->note_on(sd, midi_note, velocity, sample_rate_);
}

void SynthEngine::note_off(int midi_note)
{
    std::lock_guard<std::mutex> lock(voice_mutex_);
    for (auto& v : voices_) {
        if (v.state == Voice::State::PLAYING && v.target_note == midi_note) {
            v.note_off();
        }
    }
}

void SynthEngine::mix(float* buf, int frames)
{
    std::memset(buf, 0, frames * 2 * sizeof(float));

    std::lock_guard<std::mutex> lock(voice_mutex_);
    for (auto& v : voices_) {
        if (v.state != Voice::State::IDLE) {
            v.mix(buf, frames);
        }
    }
}

int SynthEngine::oldest_voice_idx() const
{
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].state == Voice::State::PLAYING) return i;
    }
    return -1;
}
