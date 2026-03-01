#pragma once
#include <alsa/asoundlib.h>
#include <functional>
#include <string>

struct MidiEvent {
    enum class Type { NOTE_ON, NOTE_OFF, OTHER };
    Type type     = Type::OTHER;
    int  channel  = 0;
    int  note     = 0;
    int  velocity = 0;
};

class MidiInput {
public:
    using EventCallback = std::function<void(const MidiEvent&)>;

    explicit MidiInput(const std::string& client_name = "elepiano");
    ~MidiInput();

    void set_callback(EventCallback cb) { callback_ = std::move(cb); }

    // ブロッキングループ（別スレッドで呼び出す）
    void run();
    void stop() { running_ = false; }

private:
    snd_seq_t* seq_       = nullptr;
    int        port_id_   = -1;
    bool       running_   = false;
    EventCallback callback_;
};
