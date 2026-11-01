#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace se {

// One connected executor. `socket` is a SOCKET holding an opaque handle so the
// header stays free of winsock includes.
struct WsClient {
    uintptr_t socket = 0;
    std::mutex send_mutex;
    std::atomic<bool> closed{false};
};

// Tiny RFC 6455 server: handshake, text frames, ping/pong, close.
// Binds to loopback only. One thread per client, so a slow client never stalls
// the render thread or the other clients.
class WsServer {
public:
    using TextHandler = std::function<void(const std::string&)>;
    using StatusHandler = std::function<void(int client_count)>;

    WsServer() = default;
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    bool Start(uint16_t port, TextHandler on_text, StatusHandler on_status);
    void Stop();

    void Broadcast(const std::string& text);
    int ClientCount() const;
    uint16_t Port() const;
    std::string LastError() const;

private:
    void AcceptLoop();
    void ServeClient(std::shared_ptr<WsClient> client);
    void SendFrame(WsClient& client, uint8_t opcode, const std::string& payload);
    void SetError(const std::string& error);

    TextHandler on_text_;
    StatusHandler on_status_;

    uintptr_t listen_socket_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<uint16_t> port_{0};

    std::thread accept_thread_;
    mutable std::mutex clients_mutex_;
    std::vector<std::shared_ptr<WsClient>> clients_;
    std::vector<std::thread> client_threads_;

    mutable std::mutex error_mutex_;
    std::string last_error_;
};

}  // namespace se
