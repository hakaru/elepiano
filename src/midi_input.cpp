#include "midi_input.hpp"
#include <stdexcept>
#include <cstdio>

MidiInput::MidiInput(const std::string& client_name)
{
    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_INPUT, 0) < 0)
        throw std::runtime_error("ALSA シーケンサーを開けません");

    snd_seq_set_client_name(seq_, client_name.c_str());

    port_id_ = snd_seq_create_simple_port(
        seq_, "MIDI In",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);

    if (port_id_ < 0)
        throw std::runtime_error("ALSA ポート作成失敗");

    printf("[MidiInput] client=%d port=%d\n",
           snd_seq_client_id(seq_), port_id_);
}

MidiInput::~MidiInput()
{
    if (seq_) snd_seq_close(seq_);
}

void MidiInput::run()
{
    running_ = true;
    snd_seq_event_t* ev = nullptr;

    while (running_) {
        int ret = snd_seq_event_input(seq_, &ev);
        if (ret < 0 || !ev) continue;

        MidiEvent me;
        switch (ev->type) {
        case SND_SEQ_EVENT_NOTEON:
            me.type     = MidiEvent::Type::NOTE_ON;
            me.channel  = ev->data.note.channel;
            me.note     = ev->data.note.note;
            me.velocity = ev->data.note.velocity;
            break;
        case SND_SEQ_EVENT_NOTEOFF:
            me.type     = MidiEvent::Type::NOTE_OFF;
            me.channel  = ev->data.note.channel;
            me.note     = ev->data.note.note;
            me.velocity = 0;
            break;
        default:
            me.type = MidiEvent::Type::OTHER;
            break;
        }

        if (callback_ && me.type != MidiEvent::Type::OTHER) {
            callback_(me);
        }
    }
}
