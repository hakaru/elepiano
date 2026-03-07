#include "setbfree_engine.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// setBfree C headers
extern "C" {
#include "global_inst.h"
#include "state.h"
#include "midi.h"
#include "cfgParser.h"
#include "pgmParser.h"
#include "tonegen.h"
#include "overdrive.h"
#include "reverb.h"
#include "whirl.h"
#include "vibrato.h"
#include "main.h"
}

struct SetBfreeEngine::Impl {
    b_instance inst{};
    double     sample_rate;
    int        boffset = BUFFER_SIZE_SAMPLES; // force first render
    float      bufA[BUFFER_SIZE_SAMPLES];
    float      bufB[BUFFER_SIZE_SAMPLES];
    float      bufC[BUFFER_SIZE_SAMPLES];
    float      bufL[BUFFER_SIZE_SAMPLES]; // Leslie left
    float      bufR[BUFFER_SIZE_SAMPLES]; // Leslie right
    float      tmpL[BUFFER_SIZE_SAMPLES];
    float      tmpR[BUFFER_SIZE_SAMPLES];
    SpscQueue<MidiEvent, 256> event_queue;
};

SetBfreeEngine::SetBfreeEngine(int sample_rate)
    : impl_(new Impl)
{
    impl_->sample_rate = static_cast<double>(sample_rate);
    SampleRateD = impl_->sample_rate;  // setBfree global

    // allocSynth equivalent
    impl_->inst.state   = allocRunningConfig();
    impl_->inst.progs   = allocProgs();
    impl_->inst.reverb  = allocReverb();
    impl_->inst.whirl   = allocWhirl();
    impl_->inst.midicfg = allocMidiCfg(impl_->inst.state);
    impl_->inst.synth   = allocTonegen();
    impl_->inst.preamp  = allocPreamp();

    initControllerTable(impl_->inst.midicfg);
    midiPrimeControllerMapping(impl_->inst.midicfg);

    // 設定ファイル読み込み（init 前に適用する必要がある）
    const char* cfg_path = getenv("ELEPIANO_ORGAN_CFG");
    if (!cfg_path) cfg_path = "data/organ/vb3-style.cfg";
    if (parseConfigurationFile(&impl_->inst, cfg_path) != -1) {
        fprintf(stderr, "[setBfree] config loaded: %s\n", cfg_path);
    } else {
        fprintf(stderr, "[setBfree] config not found: %s (using defaults)\n", cfg_path);
    }

    // initSynth equivalent
    initToneGenerator(impl_->inst.synth, impl_->inst.midicfg);
    initVibrato(impl_->inst.synth, impl_->inst.midicfg);
    initPreamp(impl_->inst.preamp, impl_->inst.midicfg);
    initReverb(impl_->inst.reverb, impl_->inst.midicfg, impl_->sample_rate);
    initWhirl(impl_->inst.whirl, impl_->inst.midicfg, impl_->sample_rate);
    initRunningConfig(impl_->inst.state, impl_->inst.midicfg);
    initMidiTables(impl_->inst.midicfg);

    // Default drawbars: 888000000 (Upper), 880000000 (Lower), 808... (Pedal)
    unsigned int upper[] = {8, 8, 8, 0, 0, 0, 0, 0, 0};
    unsigned int lower[] = {8, 8, 0, 0, 0, 0, 0, 0, 0};
    unsigned int pedal[] = {8, 0, 8, 0, 0, 0, 0, 0, 0};
    setDrawBars(&impl_->inst, 0, upper);
    setDrawBars(&impl_->inst, 1, lower);
    setDrawBars(&impl_->inst, 2, pedal);

    fprintf(stderr, "[setBfree] initialized at %d Hz\n", sample_rate);
}

SetBfreeEngine::~SetBfreeEngine()
{
    if (impl_) {
        freeReverb(impl_->inst.reverb);
        freeWhirl(impl_->inst.whirl);
        freeToneGenerator(impl_->inst.synth);
        freeMidiCfg(impl_->inst.midicfg);
        freePreamp(impl_->inst.preamp);
        freeProgs(impl_->inst.progs);
        freeRunningConfig(impl_->inst.state);
        delete impl_;
    }
}

void SetBfreeEngine::push_event(const MidiEvent& ev)
{
    impl_->event_queue.push(ev);
}

void SetBfreeEngine::mix(float* buf, int frames)
{
    std::memset(buf, 0, frames * 2 * sizeof(float));

    // Process queued MIDI events as raw MIDI bytes
    while (auto ev = impl_->event_queue.pop()) {
        uint8_t midi[3];
        int len = 0;
        switch (ev->type) {
        case MidiEvent::Type::NOTE_ON:
            midi[0] = 0x90 | (ev->channel & 0x0F);
            midi[1] = ev->note & 0x7F;
            midi[2] = ev->velocity & 0x7F;
            len = 3;
            break;
        case MidiEvent::Type::NOTE_OFF:
            midi[0] = 0x80 | (ev->channel & 0x0F);
            midi[1] = ev->note & 0x7F;
            midi[2] = 0;
            len = 3;
            break;
        case MidiEvent::Type::CC:
            midi[0] = 0xB0 | (ev->channel & 0x0F);
            midi[1] = ev->note & 0x7F;
            midi[2] = ev->velocity & 0x7F;
            len = 3;
            break;
        case MidiEvent::Type::PROGRAM_CHANGE:
            // Program change は elepiano 側で処理済み、setBfree には渡さない
            break;
        default:
            break;
        }
        if (len > 0) {
            parse_raw_midi_data(&impl_->inst, midi, len);
        }
    }

    // Render audio in BUFFER_SIZE_SAMPLES (128) chunks
    int written = 0;
    while (written < frames) {
        if (impl_->boffset >= BUFFER_SIZE_SAMPLES) {
            impl_->boffset = 0;
            oscGenerateFragment(impl_->inst.synth, impl_->bufA, BUFFER_SIZE_SAMPLES);
            preamp(impl_->inst.preamp, impl_->bufA, impl_->bufB, BUFFER_SIZE_SAMPLES);
            reverb(impl_->inst.reverb, impl_->bufB, impl_->bufC, BUFFER_SIZE_SAMPLES);
            whirlProc3(impl_->inst.whirl, impl_->bufC,
                       impl_->bufL, impl_->bufR,
                       impl_->tmpL, impl_->tmpR, BUFFER_SIZE_SAMPLES);
        }

        int nread = std::min(frames - written, BUFFER_SIZE_SAMPLES - impl_->boffset);
        for (int i = 0; i < nread; ++i) {
            buf[(written + i) * 2 + 0] = impl_->bufL[impl_->boffset + i];
            buf[(written + i) * 2 + 1] = impl_->bufR[impl_->boffset + i];
        }
        impl_->boffset += nread;
        written += nread;
    }
}
