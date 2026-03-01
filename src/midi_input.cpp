#include "midi_input.hpp"
#include <stdexcept>
#include <cstdio>

MidiInput::MidiInput(const std::string& client_name)
{
    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        throw std::runtime_error("ALSA シーケンサーを開けません");
    }

    snd_seq_set_client_name(seq_, client_name.c_str());

    port_id_ = snd_seq_create_simple_port(
        seq_, "MIDI In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);

    if (port_id_ < 0) {
        // ポート作成失敗: seq_ を解放してから投げる
        snd_seq_close(seq_);
        seq_ = nullptr;
        throw std::runtime_error("ALSA ポート作成失敗");
    }

    fprintf(stderr, "[MidiInput] client=%d port=%d\n",
            snd_seq_client_id(seq_), port_id_);
}

MidiInput::~MidiInput()
{
    if (seq_) snd_seq_close(seq_);
}

void MidiInput::run()
{
    running_.store(true);

    while (running_.load()) {
        snd_seq_event_t* ev = nullptr;
        int ret = snd_seq_event_input(seq_, &ev);

        if (ret < 0) {
            if (ret == -EAGAIN) continue;
            fprintf(stderr, "[MidiInput] snd_seq_event_input error: %s\n",
                    snd_strerror(ret));
            break;  // 致命的エラーはループを抜ける
        }
        if (!ev) continue;

        MidiEvent me;
        switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON: {
            const int note = ev->data.note.note;
            const int vel  = ev->data.note.velocity;
            // MIDI 標準値の範囲検証（0-127）
            if (note < 0 || note > 127 || vel < 0 || vel > 127) {
                fprintf(stderr, "[MidiInput] 範囲外 NOTE_ON: note=%d vel=%d\n", note, vel);
                continue;
            }
            me.type     = MidiEvent::Type::NOTE_ON;
            me.channel  = ev->data.note.channel & 0x0F;
            me.note     = note;
            me.velocity = vel;
            break;
        }
        case SND_SEQ_EVENT_NOTEOFF: {
            const int note = ev->data.note.note;
            if (note < 0 || note > 127) {
                fprintf(stderr, "[MidiInput] 範囲外 NOTE_OFF: note=%d\n", note);
                continue;
            }
            me.type     = MidiEvent::Type::NOTE_OFF;
            me.channel  = ev->data.note.channel & 0x0F;
            me.note     = note;
            me.velocity = 0;
            break;
        }
        default:
            me.type = MidiEvent::Type::OTHER;
            break;
        }

        if (callback_ && me.type != MidiEvent::Type::OTHER) {
            callback_(me);
        }
    }
}
