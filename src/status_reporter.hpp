#pragma once
#include <string>

class SynthEngine;
class AudioOutput;

// UNIX domain socket でステータス JSON を公開する
// ble-bridge や他のツールから接続してステータスを取得可能
class StatusReporter {
public:
    explicit StatusReporter(const std::string& socket_path);
    ~StatusReporter();

    StatusReporter(const StatusReporter&) = delete;
    StatusReporter& operator=(const StatusReporter&) = delete;

    // メインループから定期的に呼ぶ（ノンブロッキング）
    void update(const SynthEngine& synth, const AudioOutput& audio);

private:
    void accept_clients();
    void send_status(const char* json, int len);

    std::string socket_path_;
    int listen_fd_ = -1;
    static constexpr int MAX_CLIENTS = 4;
    int clients_[MAX_CLIENTS] = {-1, -1, -1, -1};
};
