#include "sample_db.hpp"
#include "synth_engine.hpp"
#include "organ_engine.hpp"
#include "setbfree_engine.hpp"
#include "midi_input.hpp"
#include "audio_output.hpp"
#include "fx_chain.hpp"
#include "status_reporter.hpp"
#include "spsc_queue.hpp"
#include <thread>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <pthread.h>

static std::atomic<bool> g_quit{false};
static bool g_midi_log = false;
static int  g_period_size = 128;
static int  g_periods     = 3;
static std::string g_wav_dump_path;
static std::string g_status_socket;
static SpscQueue<MidiEvent, 128> g_midi_log_queue;

static void sig_handler(int) { g_quit.store(true); }

// ─── MIDI イベントログ出力（--midi-log または ELEPIANO_MIDI_LOG=1 で有効） ──
// MIDI スレッドから呼ばれるため、fprintf を避けて SPSC キューに投入
static void log_midi_event(const MidiEvent& ev)
{
    if (!g_midi_log) return;
    g_midi_log_queue.push(ev);
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
    {
        // オーディオスレッドに RT スケジューリングを設定
        sched_param sp{};
        sp.sched_priority = 80;
        int ret = pthread_setschedparam(audio_thread.native_handle(), SCHED_FIFO, &sp);
        if (ret != 0)
            fprintf(stderr, "[AudioOutput] SCHED_FIFO 設定失敗 (ret=%d) — root権限が必要\n", ret);
        else
            fprintf(stderr, "[AudioOutput] SCHED_FIFO priority=80\n");
    }

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
static void run_engine(AudioOutput& audio, MidiInput& midi, std::function<void()> on_tick = nullptr)
{
    EngineGuard guard(audio, midi);

    fprintf(stderr, "起動完了。Ctrl+C で終了。\n");

    uint32_t last_xruns = 0;
    while (!g_quit.load()) {
        // MIDI ログのキュー消費（メインスレッドで安全に出力）
        while (auto ev = g_midi_log_queue.pop()) {
            switch (ev->type) {
            case MidiEvent::Type::NOTE_ON:
                fprintf(stderr, "[MIDI] NOTE ON  ch=%d note=%d vel=%d\n",
                        ev->channel, ev->note, ev->velocity);
                break;
            case MidiEvent::Type::NOTE_OFF:
                fprintf(stderr, "[MIDI] NOTE OFF ch=%d note=%d\n",
                        ev->channel, ev->note);
                break;
            case MidiEvent::Type::CC:
                fprintf(stderr, "[MIDI] CC       ch=%d cc=%d val=%d\n",
                        ev->channel, ev->note, ev->velocity);
                break;
            case MidiEvent::Type::PROGRAM_CHANGE:
                fprintf(stderr, "[MIDI] PG CHG   ch=%d pg=%d\n",
                        ev->channel, ev->note + 1);
                break;
            default: break;
            }
        }

        // Underrun カウント表示
        uint32_t xruns = audio.underrun_count();
        if (xruns != last_xruns) {
            fprintf(stderr, "[AudioOutput] underrun count: %u\n", xruns);
            last_xruns = xruns;
        }

        if (on_tick) on_tick();

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
    acfg.device        = alsa_device;
    acfg.sample_rate   = SAMPLE_RATE;
    acfg.channels      = 2;
    acfg.period_size   = g_period_size;
    acfg.periods       = g_periods;
    acfg.wav_dump_path = g_wav_dump_path;

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

// ─── 音色定義 ───────────────────────────────────────────────
struct ProgramDef {
    const char* name;
    const char* attack_json;
    const char* release_json;  // nullptr = release なし
    bool        is_organ = false;
};

// ─── ピアノモード ────────────────────────────────────────────
static int run_piano(const std::vector<ProgramDef>& programs,
                     const char* alsa_device)
{
    fprintf(stderr, "elepiano MIDI synth\n");
    for (size_t i = 0; i < programs.size(); ++i) {
        fprintf(stderr, "  PG%zu: %s\n", i + 1, programs[i].name);
    }
    fprintf(stderr, "  device: %s\n", alsa_device);

    // オルガンプログラムがあるか確認
    int organ_program = -1;
    for (size_t i = 0; i < programs.size(); ++i) {
        if (programs[i].is_organ) { organ_program = static_cast<int>(i); break; }
    }

    // 全音色のサンプルをロード（オルガンはスキップ）
    std::vector<std::unique_ptr<SampleDB>> attack_dbs;
    std::vector<std::unique_ptr<SampleDB>> release_dbs;

    for (const auto& pg : programs) {
        if (pg.is_organ) {
            attack_dbs.push_back(nullptr);
            release_dbs.push_back(nullptr);
            continue;
        }
        attack_dbs.push_back(std::make_unique<SampleDB>(pg.attack_json));
        if (pg.release_json)
            release_dbs.push_back(std::make_unique<SampleDB>(pg.release_json));
        else
            release_dbs.push_back(nullptr);
    }

    // 最初の非オルガン SampleDB からサンプルレートを取得
    int requested_rate = 44100;
    for (const auto& db : attack_dbs) {
        if (db) { requested_rate = db->sample_rate(); break; }
    }

    AudioOutput::Config acfg;
    acfg.device        = alsa_device;
    acfg.sample_rate   = requested_rate;
    acfg.channels      = 2;
    acfg.period_size   = g_period_size;
    acfg.periods       = g_periods;
    acfg.wav_dump_path = g_wav_dump_path;

    AudioOutput audio(acfg);
    const int actual_rate = audio.sample_rate();
    SynthEngine synth(actual_rate);

    for (size_t i = 0; i < programs.size(); ++i) {
        if (programs[i].is_organ) continue;  // オルガンは SynthEngine に登録しない
        synth.add_program(static_cast<int>(i),
                          attack_dbs[i].get(),
                          release_dbs[i].get(),
                          programs[i].name);
    }

    // setBfree オルガンエンジン（PG にオルガンがある場合のみ生成）
    std::unique_ptr<SetBfreeEngine> organ;
    if (organ_program >= 0) {
        organ = std::make_unique<SetBfreeEngine>(actual_rate);
    }

    // 現在のプログラム番号（MIDI/audio スレッド間共有）
    std::atomic<int> current_pg{0};

    FxChain fx(actual_rate);

    audio.set_callback([&](float* buf, int frames) {
        if (organ && current_pg.load(std::memory_order_relaxed) == organ_program) {
            organ->mix(buf, frames);
        } else {
            synth.mix(buf, frames);
        }
        fx.process(buf, frames);
    });

    // StatusReporter（--status-socket 指定時のみ）
    std::unique_ptr<StatusReporter> status;
    if (!g_status_socket.empty()) {
        status = std::make_unique<StatusReporter>(g_status_socket);
    }

    MidiInput midi("elepiano");
    midi.set_callback([&](const MidiEvent& ev) {
        log_midi_event(ev);
        if (ev.type == MidiEvent::Type::CC)
            fx.set_param(ev.note, ev.velocity);

        // プログラムチェンジを追跡
        if (ev.type == MidiEvent::Type::PROGRAM_CHANGE) {
            int pg = ev.note;
            if (pg >= 0 && pg < static_cast<int>(programs.size())) {
                current_pg.store(pg, std::memory_order_relaxed);
                fprintf(stderr, "[Main] Program Change: %d (%s)%s\n",
                        pg + 1, programs[pg].name,
                        programs[pg].is_organ ? " [ORGAN]" : "");
            }
        }

        // 適切なエンジンにルーティング
        if (organ && current_pg.load(std::memory_order_relaxed) == organ_program) {
            organ->push_event(ev);
        } else {
            synth.push_event(ev);
        }
    });

    // run_engine にステータス更新を組み込む
    run_engine(audio, midi, [&]() {
        if (status) {
            status->update(synth, audio);
        }
    });

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
        std::string arg(argv[i]);
        if (arg == "--midi-log") {
            g_midi_log = true;
            for (int j = i; j < argc - 1; ++j) argv[j] = argv[j + 1];
            --argc; --i;
        } else if (arg == "--period-size" && i + 1 < argc) {
            g_period_size = std::atoi(argv[i + 1]);
            for (int j = i; j < argc - 2; ++j) argv[j] = argv[j + 2];
            argc -= 2; --i;
        } else if (arg == "--periods" && i + 1 < argc) {
            g_periods = std::atoi(argv[i + 1]);
            for (int j = i; j < argc - 2; ++j) argv[j] = argv[j + 2];
            argc -= 2; --i;
        } else if (arg == "--wav-dump" && i + 1 < argc) {
            g_wav_dump_path = argv[i + 1];
            for (int j = i; j < argc - 2; ++j) argv[j] = argv[j + 2];
            argc -= 2; --i;
        } else if (arg == "--status-socket" && i + 1 < argc) {
            g_status_socket = argv[i + 1];
            for (int j = i; j < argc - 2; ++j) argv[j] = argv[j + 2];
            argc -= 2; --i;
        }
    }

    // CLI 引数バリデーション
    if (g_period_size < 16 || g_period_size > 8192) {
        fprintf(stderr, "[WARNING] --period-size %d は範囲外 (16-8192)、デフォルト 128 を使用\n", g_period_size);
        g_period_size = 128;
    }
    if (g_periods < 2 || g_periods > 8) {
        fprintf(stderr, "[WARNING] --periods %d は範囲外 (2-8)、デフォルト 3 を使用\n", g_periods);
        g_periods = 3;
    }

    // --organ [alsa_device]
    if (argc > 1 && std::string(argv[1]) == "--organ") {
        const char* alsa_device = (argc > 2) ? argv[2] : "default";
        return run_organ(alsa_device);
    }

    // 音色定義: --pg attack_json [release_json] で追加、最後に alsa_device
    // 互換: 旧形式 attack_json [release_json] alsa_device もサポート
    std::vector<ProgramDef> programs;
    const char* alsa_device = "default";

    int i = 1;
    while (i < argc) {
        std::string arg(argv[i]);
        if (arg == "--pg" && i + 1 < argc) {
            std::string next(argv[i + 1]);
            if (next == "--organ") {
                // --pg --organ → オルガンプログラム
                ProgramDef pg;
                pg.name = "Organ";
                pg.attack_json = nullptr;
                pg.release_json = nullptr;
                pg.is_organ = true;
                i += 2;
                programs.push_back(pg);
            } else {
                ProgramDef pg;
                pg.name = argv[i + 1];
                pg.attack_json = argv[i + 1];
                pg.release_json = nullptr;
                i += 2;
                // 次の引数が --pg でも alsa デバイスでもなければ release_json
                if (i < argc && std::string(argv[i]) != "--pg" &&
                    std::string(argv[i]).find("hw:") == std::string::npos &&
                    std::string(argv[i]) != "default") {
                    pg.release_json = argv[i];
                    ++i;
                }
                programs.push_back(pg);
            }
        } else {
            // 最後の引数は alsa_device
            // それ以外は旧形式互換
            if (programs.empty()) {
                // 旧形式: attack [release] device
                ProgramDef pg;
                pg.name = argv[i];
                pg.attack_json = argv[i];
                pg.release_json = nullptr;
                ++i;
                if (i < argc && std::string(argv[i]).find("hw:") == std::string::npos &&
                    std::string(argv[i]) != "default") {
                    pg.release_json = argv[i];
                    ++i;
                }
                programs.push_back(pg);
                if (i < argc) {
                    alsa_device = argv[i];
                    ++i;
                }
            } else {
                alsa_device = argv[i];
                ++i;
            }
        }
    }

    if (programs.empty()) {
        programs.push_back({"Rhodes", "data/rhodes-classic-clean/samples.json", nullptr});
    }

    return run_piano(programs, alsa_device);
}
