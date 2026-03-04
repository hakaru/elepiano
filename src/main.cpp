#include "sample_db.hpp"
#include "synth_engine.hpp"
#include "organ_engine.hpp"
#include "midi_input.hpp"
#include "audio_output.hpp"
#include <thread>
#include <csignal>
#include <cstdio>
#include <atomic>
#include <memory>
#include <string>

static std::atomic<bool> g_quit{false};
static bool g_midi_log = false;

static void sig_handler(int) { g_quit.store(true); }

// ─── MIDI イベントログ出力（--midi-log または ELEPIANO_MIDI_LOG=1 で有効） ──
static void log_midi_event(const MidiEvent& ev)
{
    if (!g_midi_log) return;
    switch (ev.type) {
    case MidiEvent::Type::NOTE_ON:
        fprintf(stderr, "[MIDI] NOTE ON  ch=%d note=%d vel=%d\n",
                ev.channel, ev.note, ev.velocity);
        break;
    case MidiEvent::Type::NOTE_OFF:
        fprintf(stderr, "[MIDI] NOTE OFF ch=%d note=%d\n",
                ev.channel, ev.note);
        break;
    case MidiEvent::Type::CC:
        fprintf(stderr, "[MIDI] CC       ch=%d cc=%d val=%d\n",
                ev.channel, ev.note, ev.velocity);
        break;
    default:
        break;
    }
}

// ─── RAII スレッドライフサイクル管理 ──────────────────────────
// AudioOutput / MidiInput のスレッドを起動し、
// デストラクタで stop + join を保証する。
struct EngineGuard {
    AudioOutput& audio;
    MidiInput&   midi;
    std::thread  midi_thread;
    std::thread  audio_thread;

    EngineGuard(AudioOutput& a, MidiInput& m)
        : audio(a), midi(m),
          midi_thread([&]()  { midi.run(); }),
          audio_thread([&]() { audio.run(); })
    {}

    ~EngineGuard() {
        midi.stop();
        audio.stop();
        if (midi_thread.joinable())  midi_thread.join();
        if (audio_thread.joinable()) audio_thread.join();
    }

    // コピー/ムーブ禁止
    EngineGuard(const EngineGuard&)            = delete;
    EngineGuard& operator=(const EngineGuard&) = delete;
};

// MIDI/Audio スレッド管理の共通部分
static void run_engine(AudioOutput& audio, MidiInput& midi)
{
    EngineGuard guard(audio, midi);

    fprintf(stderr, "起動完了。Ctrl+C で終了。\n");

    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "\n終了中...\n");
    // guard のデストラクタが stop + join を実行
}

// ─── オルガンモード ────────────────────────────────────────────
static int run_organ(const char* alsa_device)
{
    fprintf(stderr, "elepiano MIDI organ (tonewheel, 2 manuals + pedal)\n");
    fprintf(stderr, "  device: %s\n", alsa_device);
    fprintf(stderr, "  CH1: Upper Manual  drawbars=[0,0,8,8,6,0,0,0,0]  8'+4'+2/3'\n");
    fprintf(stderr, "  CH2: Lower Manual  drawbars=[0,0,8,8,0,0,0,0,0]  8'+4'\n");
    fprintf(stderr, "  CH3: Pedal Board   drawbars=[8,0,8,0,0,0,0,0,0]  16'+8'\n");
    fprintf(stderr, "  CC 64: Leslie Fast(6.7Hz)/Slow(0.7Hz)\n");
    fprintf(stderr, "  CC  7: Volume per manual\n");
    fprintf(stderr, "  CC 12-20: Drawbars 0-8 (by channel)\n");

    static constexpr int SAMPLE_RATE = 44100;

    AudioOutput::Config acfg;
    acfg.device      = alsa_device;
    acfg.sample_rate = SAMPLE_RATE;
    acfg.channels    = 2;
    acfg.period_size = 256;
    acfg.periods     = 4;

    AudioOutput audio(acfg);
    OrganEngine organ(SAMPLE_RATE);

    audio.set_callback([&](float* buf, int frames) {
        organ.mix(buf, frames);
    });

    MidiInput midi("elepiano-organ");
    midi.set_callback([&](const MidiEvent& ev) {
        log_midi_event(ev);
        organ.push_event(ev);
    });

    run_engine(audio, midi);
    return 0;
}

// ─── ピアノモード ────────────────────────────────────────────
static int run_piano(const char* json_path,
                     const char* release_json_path,
                     const char* alsa_device)
{
    fprintf(stderr, "elepiano MIDI synth\n");
    fprintf(stderr, "  attack samples:  %s\n", json_path);
    if (release_json_path)
        fprintf(stderr, "  release samples: %s\n", release_json_path);
    fprintf(stderr, "  device:          %s\n", alsa_device);

    SampleDB db(json_path);

    std::unique_ptr<SampleDB> release_db;
    if (release_json_path) {
        release_db = std::make_unique<SampleDB>(release_json_path);
    }

    AudioOutput::Config acfg;
    acfg.device      = alsa_device;
    acfg.sample_rate = db.sample_rate();
    acfg.channels    = 2;
    acfg.period_size = 256;
    acfg.periods     = 4;

    AudioOutput audio(acfg);
    SynthEngine synth(db, acfg.sample_rate, release_db.get());

    audio.set_callback([&](float* buf, int frames) {
        synth.mix(buf, frames);
    });

    MidiInput midi("elepiano");
    midi.set_callback([&](const MidiEvent& ev) {
        log_midi_event(ev);
        synth.push_event(ev);
    });

    run_engine(audio, midi);
    return 0;
}

// ─── エントリーポイント ──────────────────────────────────────
int main(int argc, char* argv[])
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    // --midi-log フラグまたは環境変数で MIDI ログを有効化
    if (const char* env = getenv("ELEPIANO_MIDI_LOG"); env && env[0] == '1')
        g_midi_log = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--midi-log") {
            g_midi_log = true;
            for (int j = i; j < argc - 1; ++j) argv[j] = argv[j + 1];
            --argc; --i;
        }
    }

    // --organ [alsa_device]
    if (argc > 1 && std::string(argv[1]) == "--organ") {
        const char* alsa_device = (argc > 2) ? argv[2] : "default";
        return run_organ(alsa_device);
    }

    // [attack_json] [release_json] [alsa_device]
    const char* json_path         = "data/rhodes-classic/samples.json";
    const char* release_json_path = nullptr;
    const char* alsa_device       = "default";

    if (argc > 1) json_path         = argv[1];
    if (argc > 2 && argv[2][0] != '\0') release_json_path = argv[2];
    if (argc > 3) alsa_device       = argv[3];

    return run_piano(json_path, release_json_path, alsa_device);
}
