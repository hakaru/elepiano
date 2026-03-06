#include "status_reporter.hpp"
#include "synth_engine.hpp"
#include "audio_output.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

StatusReporter::StatusReporter(const std::string& socket_path)
    : socket_path_(socket_path)
{
    unlink(socket_path_.c_str());

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        fprintf(stderr, "[StatusReporter] socket() failed: %s\n", strerror(errno));
        return;
    }

    // ノンブロッキング
    fcntl(listen_fd_, F_SETFL, O_NONBLOCK);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[StatusReporter] bind(%s) failed: %s\n",
                socket_path_.c_str(), strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (listen(listen_fd_, 4) < 0) {
        fprintf(stderr, "[StatusReporter] listen() failed: %s\n", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    fprintf(stderr, "[StatusReporter] listening on %s\n", socket_path_.c_str());
}

StatusReporter::~StatusReporter()
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients_[i] >= 0) close(clients_[i]);
    }
    if (listen_fd_ >= 0) close(listen_fd_);
    unlink(socket_path_.c_str());
}

void StatusReporter::accept_clients()
{
    if (listen_fd_ < 0) return;
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) break;
        fcntl(fd, F_SETFL, O_NONBLOCK);

        // 空きスロットに追加
        bool added = false;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients_[i] < 0) {
                clients_[i] = fd;
                added = true;
                break;
            }
        }
        if (!added) {
            close(fd);  // スロット満杯
        }
    }
}

void StatusReporter::send_status(const char* json, int len)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients_[i] < 0) continue;
        // 改行区切りで送信
        ssize_t r = write(clients_[i], json, len);
        if (r < 0) {
            // 切断されたクライアントを除去
            close(clients_[i]);
            clients_[i] = -1;
            continue;
        }
        char nl = '\n';
        write(clients_[i], &nl, 1);
    }
}

void StatusReporter::update(const SynthEngine& synth, const AudioOutput& audio)
{
    accept_clients();

    // 接続クライアントがなければスキップ
    bool has_client = false;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients_[i] >= 0) { has_client = true; break; }
    }
    if (!has_client) return;

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "{\"running\":true,\"program\":%d,\"name\":\"%s\","
        "\"voices\":%d,\"underruns\":%u}",
        synth.current_program() + 1,
        synth.current_program_name(),
        synth.active_voice_count(),
        audio.underrun_count());

    if (len > 0 && len < (int)sizeof(buf)) {
        send_status(buf, len);
    }
}
