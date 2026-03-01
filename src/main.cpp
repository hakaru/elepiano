#include "sample_db.hpp"
#include "synth_engine.hpp"
#include "midi_input.hpp"
#include "audio_output.hpp"
#include <thread>
#include <csignal>
#include <cstdio>
#include <atomic>

static std::atomic<bool> g_quit{false};

static void sig_handler(int) { g_quit.store(true); }

int main(int argc, char* argv[])
{
    const char* json_path    = "data/rhodes/samples.json";
    const char* alsa_device  = "hw:pisound";

    if (argc > 1) json_path   = argv[1];
    if (argc > 2) alsa_device = argv[2];

    fprintf(stderr, "elepiano MIDI synth\n");
    fprintf(stderr, "  samples: %s\n", json_path);
    fprintf(stderr, "  device:  %s\n", alsa_device);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    SampleDB db(json_path);

    AudioOutput::Config acfg;
    acfg.device      = alsa_device;
    acfg.sample_rate = db.sample_rate();
    acfg.channels    = 2;
    acfg.period_size = 256;
    acfg.periods     = 4;  // 23.2ms バッファ（アンダーラン対策）

    AudioOutput audio(acfg);
    SynthEngine synth(db, acfg.sample_rate);

    // オーディオスレッドのコールバック — fprintf 禁止
    audio.set_callback([&](float* buf, int frames) {
        synth.mix(buf, frames);
    });

    // MIDI コールバック — MIDI スレッドで実行
    // ロックフリー push_event() で SynthEngine に渡す（mutex 不要）
    MidiInput midi("elepiano");
    midi.set_callback([&](const MidiEvent& ev) {
        if (ev.type == MidiEvent::Type::NOTE_ON) {
            fprintf(stderr, "[MIDI] NOTE ON  ch=%d note=%d vel=%d\n",
                    ev.channel, ev.note, ev.velocity);
        } else if (ev.type == MidiEvent::Type::NOTE_OFF) {
            fprintf(stderr, "[MIDI] NOTE OFF ch=%d note=%d\n",
                    ev.channel, ev.note);
        }
        synth.push_event(ev);
    });

    std::thread midi_thread([&]()  { midi.run(); });
    std::thread audio_thread([&]() { audio.run(); });

    fprintf(stderr, "起動完了。Ctrl+C で終了。\n");

    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "\n終了中...\n");
    midi.stop();
    audio.stop();

    if (midi_thread.joinable())  midi_thread.join();
    if (audio_thread.joinable()) audio_thread.join();

    return 0;
}
