#pragma once
#include "spsc_queue.hpp"
#include <cstdio>
#include <cstdint>

/// RT スレッドから安全にログを送るためのエントリ
struct RtLogEntry {
    enum class Tag : uint8_t {
        PROGRAM_CHANGE,    // arg1=program, arg2=release_ms
        MOD_MODE,          // arg1: 0=Tremolo, 1=Phaser
        SPACE_MODE,        // arg1: mode index (0-3)
        IR_SELECT,         // arg1: IR index
        PCM_RECOVER_FAIL,  // arg1: ALSA error code
    };
    Tag tag;
    int arg1 = 0;
    int arg2 = 0;
};

/// グローバル RT ログキュー（audio thread → main thread）
inline SpscQueue<RtLogEntry, 64>& rt_log_queue() {
    static SpscQueue<RtLogEntry, 64> q;
    return q;
}

/// RT スレッドから呼ぶ（lock-free push）
inline void rt_log(RtLogEntry::Tag tag, int a1 = 0, int a2 = 0) {
    rt_log_queue().push({tag, a1, a2});
}

/// メインスレッドから呼ぶ（キューを drain して stderr に出力）
inline void drain_rt_log() {
    while (auto e = rt_log_queue().pop()) {
        switch (e->tag) {
        case RtLogEntry::Tag::PROGRAM_CHANGE:
            std::fprintf(stderr, "[SynthEngine] Program Change: %d (release_fade=%dms)\n",
                         e->arg1 + 1, e->arg2);
            break;
        case RtLogEntry::Tag::MOD_MODE:
            std::fprintf(stderr, "[FxChain] Mod: %s\n",
                         e->arg1 == 0 ? "Tremolo" : "Phaser");
            break;
        case RtLogEntry::Tag::SPACE_MODE: {
            static const char* names[] = {"Off", "Tape Echo", "Room Reverb", "Plate Reverb"};
            if (e->arg1 >= 0 && e->arg1 <= 3)
                std::fprintf(stderr, "[FxChain] Space: %s\n", names[e->arg1]);
            break;
        }
        case RtLogEntry::Tag::IR_SELECT:
            std::fprintf(stderr, "[IR] selected: [%d]\n", e->arg1);
            break;
        case RtLogEntry::Tag::PCM_RECOVER_FAIL:
            std::fprintf(stderr, "[AudioOutput] snd_pcm_recover failed: %d\n", e->arg1);
            break;
        }
    }
}
