#pragma once
#include <alsa/asoundlib.h>
#include <functional>
#include <string>
#include <atomic>

struct MidiEvent {
    enum class Type { NOTE_ON, NOTE_OFF, CC, OTHER };
    Type type     = Type::OTHER;
    int  channel  = 0;
    int  note     = 0;  // CC イベント時は cc_number として使用
    int  velocity = 0;  // CC イベント時は cc_value として使用
};

class MidiInput {
public:
    using EventCallback = std::function<void(const MidiEvent&)>;

    explicit MidiInput(const std::string& client_name = "elepiano");
    ~MidiInput();

    // コピー/ムーブ禁止（ALSA ハンドルを生ポインタで保持するため）
    MidiInput(const MidiInput&)            = delete;
    MidiInput& operator=(const MidiInput&) = delete;
    MidiInput(MidiInput&&)                 = delete;
    MidiInput& operator=(MidiInput&&)      = delete;

    void set_callback(EventCallback cb) { callback_ = std::move(cb); }

    void run();
    void stop() { running_.store(false); }

private:
    snd_seq_t*          seq_     = nullptr;
    int                 port_id_ = -1;
    std::atomic<bool>   running_{false};
    EventCallback       callback_;
};
