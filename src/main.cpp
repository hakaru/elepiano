#include "sample_db.hpp"
#include "synth_engine.hpp"
#include "midi_input.hpp"
#include "audio_output.hpp"
#include <thread>
#include <csignal>
#include <cstdio>
#include <cstdlib>

static volatile bool g_quit = false;

static void sig_handler(int) { g_quit = true; }

int main(int argc, char* argv[])
{
    // ============================================================
    // 引数
    // ============================================================
    const char* json_path    = "data/rhodes/samples.json";
    const char* alsa_device  = "hw:pisound";

    if (argc > 1) json_path   = argv[1];
    if (argc > 2) alsa_device = argv[2];

    printf("elepiano MIDI synth\n");
    printf("  samples: %s\n", json_path);
    printf("  device:  %s\n", alsa_device);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    // ============================================================
    // 初期化
    // ============================================================
    SampleDB db(json_path);

    AudioOutput::Config acfg;
    acfg.device      = alsa_device;
    acfg.sample_rate = db.sample_rate();
    acfg.channels    = 2;
    acfg.period_size = 256;
    acfg.periods     = 3;

    AudioOutput audio(acfg);
    SynthEngine synth(db, acfg.sample_rate);

    // オーディオコールバックを登録
    audio.set_callback([&](float* buf, int frames) {
        synth.mix(buf, frames);
    });

    // MIDI 入力
    MidiInput midi("elepiano");
    midi.set_callback([&](const MidiEvent& ev) {
        if (ev.type == MidiEvent::Type::NOTE_ON) {
            printf("[MIDI] NOTE ON  ch=%d note=%d vel=%d\n",
                   ev.channel, ev.note, ev.velocity);
            synth.note_on(ev.note, ev.velocity);
        } else if (ev.type == MidiEvent::Type::NOTE_OFF) {
            printf("[MIDI] NOTE OFF ch=%d note=%d\n",
                   ev.channel, ev.note);
            synth.note_off(ev.note);
        }
    });

    // ============================================================
    // スレッド起動
    // ============================================================
    std::thread midi_thread([&]() { midi.run(); });
    std::thread audio_thread([&]() { audio.run(); });

    printf("起動完了。Ctrl+C で終了。\n");

    // メインスレッドはシグナル待ち
    while (!g_quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("\n終了中...\n");
    midi.stop();
    audio.stop();

    if (midi_thread.joinable())  midi_thread.join();
    if (audio_thread.joinable()) audio_thread.join();

    return 0;
}
