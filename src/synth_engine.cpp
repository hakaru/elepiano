#include "synth_engine.hpp"
#include <cstring>
#include <climits>
#include <cstdio>

SynthEngine::SynthEngine(int sample_rate)
    : sample_rate_(sample_rate)
{}

void SynthEngine::add_program(int program, SampleDB* attack, SampleDB* release)
{
    if (program < 0 || program >= MAX_PROGRAMS) return;
    programs_[program] = {attack, release};
    // 最初に登録された音色をデフォルトにする
    if (!db_) {
        db_ = attack;
        release_db_ = release;
        current_program_ = program;
    }
}

void SynthEngine::push_event(const MidiEvent& ev)
{
    if (!event_queue_.push(ev) && ev.type == MidiEvent::Type::NOTE_OFF) {
        fprintf(stderr, "[SynthEngine] WARNING: queue full, NOTE_OFF dropped (ch=%d note=%d)\n",
                ev.channel, ev.note);
    }
}

void SynthEngine::mix(float* buf, int frames)
{
    std::memset(buf, 0, frames * 2 * sizeof(float));

    while (auto ev = event_queue_.pop()) {
        if (ev->type == MidiEvent::Type::NOTE_ON) {
            _note_on(ev->note, ev->velocity);
        } else if (ev->type == MidiEvent::Type::NOTE_OFF) {
            _note_off(ev->note);
        } else if (ev->type == MidiEvent::Type::CC) {
            _cc(ev->note, ev->velocity);
        } else if (ev->type == MidiEvent::Type::PROGRAM_CHANGE) {
            _program_change(ev->note);
        }
    }

    for (auto& v : voices_) {
        if (v.state != Voice::State::IDLE) {
            v.mix(buf, frames);
        }
    }

    // ポリフォニーによるクリッピング防止
    const int total = frames * 2;
    for (int i = 0; i < total; ++i) {
        float x = buf[i];
        if (x > 1.0f)       buf[i] = 1.0f;
        else if (x < -1.0f) buf[i] = -1.0f;
        else                 buf[i] = x * (1.5f - 0.5f * x * x);
    }

    sample_counter_ += static_cast<uint64_t>(frames);
}

void SynthEngine::_note_on(int midi_note, int velocity)
{
    if (velocity == 0) {
        _note_off(midi_note);
        return;
    }

    if (!db_) return;
    const SampleData* sd = db_->find(midi_note, velocity);
    if (!sd) return;

    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.state == Voice::State::IDLE) { target = &v; break; }
    }

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
            if (sustain_held_) {
                v.sustained = true;
                continue;
            }
            if (release_db_) {
                _start_release_voice(midi_note, v.velocity);
            }
            v.note_off();
        }
    }
}

void SynthEngine::_cc(int cc_num, int cc_val)
{
    if (cc_num == 64) {
        bool new_state = (cc_val >= 64);
        if (sustain_held_ && !new_state) {
            for (auto& v : voices_) {
                if (v.sustained && v.state == Voice::State::PLAYING && !v.is_release_voice) {
                    if (release_db_) {
                        _start_release_voice(v.target_note, v.velocity);
                    }
                    v.note_off();
                    v.sustained = false;
                }
            }
        }
        sustain_held_ = new_state;
    }
}

void SynthEngine::_program_change(int program)
{
    if (program < 0 || program >= MAX_PROGRAMS) return;
    if (!programs_[program].attack) return;  // 未登録の音色は無視

    // 全ボイスを停止（音色切替時のノイズ防止）
    for (auto& v : voices_) {
        v.state = Voice::State::IDLE;
    }
    sustain_held_ = false;

    current_program_ = program;
    db_ = programs_[program].attack;
    release_db_ = programs_[program].release;

    fprintf(stderr, "[SynthEngine] Program Change: %d\n", program + 1);
}

void SynthEngine::_start_release_voice(int midi_note, int velocity)
{
    if (!release_db_) return;
    const SampleData* sd = release_db_->find(midi_note, velocity);
    if (!sd) return;

    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (v.state == Voice::State::IDLE) { target = &v; break; }
    }

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

    for (int i = 0; i < MAX_VOICES; ++i) {
        if (voices_[i].state == Voice::State::PLAYING &&
            !voices_[i].is_release_voice &&
            voices_[i].start_time_samples < min_time) {
            min_time = voices_[i].start_time_samples;
            oldest = i;
        }
    }
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
