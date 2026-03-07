#include "synth_engine.hpp"
#include "rt_log.hpp"
#include <cstring>
#include <climits>
#include <cstdio>
#include <cmath>

// ── PedalClick 実装（機械的クリック — Pedal Up 用） ──

void PedalClick::trigger(int sr)
{
    float atk_s = attack_ms * 0.001f;
    float rel_s = release_ms * 0.001f;
    attack_rate  = (atk_s > 0.0005f) ? 1.0f / (atk_s * sr) : 0.0f;
    release_rate = 1.0f / (rel_s * sr);
    phase = (attack_rate > 0.0f) ? Phase::ATTACK : Phase::RELEASE;
    env   = (attack_rate > 0.0f) ? 0.0f : 1.0f;
    // LPF ~1200Hz, HPF ~400Hz → 帯域 400-1200Hz（高域クリック）
    lp_coeff = 1.0f - std::exp(-2.0f * 3.14159f * 1200.0f / sr);
    hp_coeff = 1.0f - 1.0f / (sr / (2.0f * 3.14159f * 400.0f) + 1.0f);
    lpf_state = 0.0f;
    hpf_state = 0.0f;
    hpf_prev  = 0.0f;
}

void PedalClick::mix(float* buf, int frames)
{
    if (phase == Phase::IDLE) return;
    for (int i = 0; i < frames; ++i) {
        if (phase == Phase::ATTACK) {
            env += attack_rate;
            if (env >= 1.0f) { env = 1.0f; phase = Phase::RELEASE; }
        } else {
            env -= release_rate;
            if (env <= 0.0f) { phase = Phase::IDLE; return; }
        }

        float noise = next_noise();
        lpf_state += lp_coeff * (noise - lpf_state);
        float hp_out = hp_coeff * (hpf_state + lpf_state - hpf_prev);
        hpf_prev  = lpf_state;
        hpf_state = hp_out;

        float out = hp_out * env * volume;
        buf[i * 2 + 0] += out;
        buf[i * 2 + 1] += out;
    }
}

// ── PedalResonance 実装（弦共鳴 — Pedal Down 用） ──
// 現在発音中のノートの周波数 + 倍音で共鳴

void PedalResonance::trigger_down(int sr, const int* active_notes, int num_notes)
{
    float atk_s = attack_ms * 0.001f;
    float rel_s = release_ms * 0.001f;
    attack_rate  = (atk_s > 0.001f) ? 1.0f / (atk_s * sr) : 0.0f;
    release_rate = 1.0f / (rel_s * sr);
    phase = (attack_rate > 0.0f) ? Phase::ATTACK : Phase::SUSTAIN;
    env   = (attack_rate > 0.0f) ? 0.0f : 1.0f;

    num_partials = 0;

    if (num_notes == 0) {
        // ノートなし → 軽い環境共鳴（低域3本だけ）
        static const float ambient[] = { 55.0f, 110.0f, 82.4f };
        for (int i = 0; i < 3 && num_partials < MAX_PARTIALS; ++i) {
            incs[num_partials]   = static_cast<double>(ambient[i]) / sr;
            amps[num_partials]   = 0.3f;
            phases[num_partials] = i * 0.31;
            num_partials++;
        }
    } else {
        // 各ノートの基音 + オクターブ上 + 5度上で共鳴
        for (int n = 0; n < num_notes && num_partials < MAX_PARTIALS; ++n) {
            float base_freq = 440.0f * std::pow(2.0f, (active_notes[n] - 69) / 12.0f);
            float vel_scale = 1.0f;  // 全ノート均等（既にvelocityで鳴ってる）

            // 基音（1倍音）
            if (num_partials < MAX_PARTIALS) {
                incs[num_partials]   = static_cast<double>(base_freq) / sr;
                amps[num_partials]   = 0.6f * vel_scale;
                phases[num_partials] = n * 0.23;
                num_partials++;
            }
            // オクターブ上（2倍音）
            if (num_partials < MAX_PARTIALS) {
                incs[num_partials]   = static_cast<double>(base_freq * 2.0f) / sr;
                amps[num_partials]   = 0.25f * vel_scale;
                phases[num_partials] = n * 0.23 + 0.37;
                num_partials++;
            }
            // 5度上（3倍音 ≈ 完全5度+オクターブ）
            if (num_partials < MAX_PARTIALS) {
                incs[num_partials]   = static_cast<double>(base_freq * 3.0f) / sr;
                amps[num_partials]   = 0.12f * vel_scale;
                phases[num_partials] = n * 0.23 + 0.61;
                num_partials++;
            }
        }
    }
}

void PedalResonance::trigger_up()
{
    if (phase != Phase::IDLE) {
        phase = Phase::RELEASE;
    }
}

void PedalResonance::mix(float* buf, int frames)
{
    if (phase == Phase::IDLE || num_partials == 0) return;

    for (int i = 0; i < frames; ++i) {
        if (phase == Phase::ATTACK) {
            env += attack_rate;
            if (env >= 1.0f) { env = 1.0f; phase = Phase::SUSTAIN; }
        } else if (phase == Phase::RELEASE) {
            env -= release_rate;
            if (env <= 0.0f) { phase = Phase::IDLE; return; }
        }

        float sum = 0.0f;
        for (int p = 0; p < num_partials; ++p) {
            double ph = phases[p] - static_cast<int>(phases[p]);
            double x = ph * 2.0 - 1.0;
            double ax = (x < 0) ? -x : x;
            double s = 4.0 * x * (1.0 - ax);
            sum += static_cast<float>(s) * amps[p];
            phases[p] += incs[p];
        }

        float out = sum * env * volume * 0.12f;
        buf[i * 2 + 0] += out;
        buf[i * 2 + 1] += out;
    }
}

// ── SynthEngine 実装 ──

SynthEngine::SynthEngine(int sample_rate)
    : sample_rate_(sample_rate)
{}

int SynthEngine::active_voice_count() const
{
    int count = 0;
    for (const auto& v : voices_) {
        if (v.state != Voice::State::IDLE) ++count;
    }
    return count;
}

const char* SynthEngine::current_program_name() const
{
    const auto& pg = programs_[current_program_];
    return pg.name ? pg.name : "Unknown";
}

void SynthEngine::add_program(int program, SampleDB* attack, SampleDB* release,
                               const char* name)
{
    if (program < 0 || program >= MAX_PROGRAMS) return;
    programs_[program] = {attack, release, name};
    // 最初に登録された音色をデフォルトにする
    if (!db_) {
        db_ = attack;
        release_db_ = release;
        current_program_ = program;
        release_time_s_ = release ? 0.050f : 0.200f;
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
        } else if (ev->type == MidiEvent::Type::PITCH_BEND) {
            _pitch_bend(ev->velocity);
        } else if (ev->type == MidiEvent::Type::PROGRAM_CHANGE) {
            _program_change(ev->note);
        }
    }

    for (auto& v : voices_) {
        if (v.state != Voice::State::IDLE) {
            v.mix(buf, frames, bend_ratio_);
        }
    }

    // ペダル音（弦共鳴 + クリック）
    // pedal_resonance_.mix(buf, frames);
    // pedal_click_.mix(buf, frames);

    // マスターゲイン (CC7 × CC11) + クリッピング防止
    const float master = volume_ * expression_;
    const int total = frames * 2;
    for (int i = 0; i < total; ++i) {
        float x = buf[i] * master;
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

    target->note_on(sd, midi_note, velocity, sample_rate_, release_time_s_);
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
            v.note_off();
        }
    }
}

void SynthEngine::_cc(int cc_num, int cc_val)
{
    // CC7 / CC102: Volume
    if (cc_num == 7 || cc_num == 102) {
        volume_ = cc_val / 127.0f;
        return;
    }
    // CC11 / CC103: Expression
    if (cc_num == 11 || cc_num == 103) {
        expression_ = cc_val / 127.0f;
        return;
    }
    // CC5: リリースタイム調整 (0=0ms, 127=200ms)
    if (cc_num == 5) {
        release_time_s_ = (cc_val / 127.0f) * 0.200f;
        return;
    }

    if (cc_num == 64) {
        bool new_state = (cc_val >= 64);
        if (sustain_held_ && !new_state) {
            for (auto& v : voices_) {
                if (v.sustained && v.state == Voice::State::PLAYING && !v.is_release_voice) {
                    v.note_off();
                    v.sustained = false;
                }
            }
        }
        sustain_held_ = new_state;
    }
}

void SynthEngine::_pitch_bend(int value)
{
    // value: 0..16383, center=8192, ±2 semitones
    float semitones = ((value - 8192) / 8192.0f) * 2.0f;
    bend_ratio_ = std::pow(2.0f, semitones / 12.0f);
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

    // release サンプルがない音色はフェード長め (200ms)、ある音色は短め (50ms)
    release_time_s_ = release_db_ ? 0.050f : 0.200f;

    rt_log(RtLogEntry::Tag::PROGRAM_CHANGE, program,
           static_cast<int>(release_time_s_ * 1000));
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

    target->note_on(sd, midi_note, velocity, sample_rate_, release_time_s_);
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
