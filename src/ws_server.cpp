#include "ws_server.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#include "log.h"
#include "protocol.h"
#include "sha1.h"

namespace se {
namespace {

bool RecvExact(SOCKET socket, void* buffer, size_t length) {
    char* cursor = static_cast<char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        const int received = recv(socket, cursor, static_cast<int>(remaining), 0);
        if (received <= 0) return false;
        cursor += received;
        remaining -= static_cast<size_t>(received);
    }
    return true;
}

bool SendExact(SOCKET socket, const void* buffer, size_t length) {
    const char* cursor = static_cast<const char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        const int sent = send(socket, cursor, static_cast<int>(remaining), 0);
        if (sent <= 0) return false;
        cursor += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

WsServer::~WsServer() { Stop(); }

void WsServer::SetError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
}

std::string WsServer::LastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

int WsServer::ClientCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return static_cast<int>(clients_.size());
}

uint16_t WsServer::Port() const { return port_; }

bool WsServer::Start(uint16_t port, TextHandler on_text, StatusHandler on_status) {
    if (running_) return true;

    on_text_ = std::move(on_text);
    on_status_ = std::move(on_status);

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        SetError("WSAStartup failed");
        return false;
    }

    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        SetError("socket() failed");
        WSACleanup();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof reuse);

    sockaddr_in address;
    std::memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == SOCKET_ERROR) {
        SetError("bind() failed on port " + std::to_string(port) + " (already in use?)");
        closesocket(listener);
        WSACleanup();
        return false;
    }

    if (listen(listener, 8) == SOCKET_ERROR) {
        SetError("listen() failed");
        closesocket(listener);
        WSACleanup();
        return false;
    }

    int address_length = sizeof address;
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length) == 0) {
        port_ = ntohs(address.sin_port);
    } else {
        port_ = port;
    }

    listen_socket_ = static_cast<uintptr_t>(listener);
    running_ = true;
    accept_thread_ = std::thread(&WsServer::AcceptLoop, this);
    LogLine("websocket server listening on ws://127.0.0.1:%u", static_cast<unsigned>(port_.load()));
    return true;
}

void WsServer::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    const SOCKET listener = static_cast<SOCKET>(listen_socket_);
    listen_socket_ = 0;
    if (listener != 0) {
        closesocket(listener);
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::vector<std::shared_ptr<WsClient>> clients;
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.swap(clients_);
        threads.swap(client_threads_);
    }

    for (const auto& client : clients) {
        if (client->closed.exchange(true)) continue;
        const SOCKET socket = static_cast<SOCKET>(client->socket);
        if (socket != 0) {
            shutdown(socket, SD_BOTH);
            closesocket(socket);
        }
    }
    for (auto& thread : threads) {
        if (thread.joinable()) thread.join();
    }

    WSACleanup();
    LogLine("websocket server stopped");
}

void WsServer::AcceptLoop() {
    while (running_) {
        sockaddr_in address;
        int address_length = sizeof address;
        const SOCKET accepted = accept(static_cast<SOCKET>(listen_socket_),
                                       reinterpret_cast<sockaddr*>(&address), &address_length);
        if (accepted == INVALID_SOCKET) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        auto client = std::make_shared<WsClient>();
        client->socket = static_cast<uintptr_t>(accepted);
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(client);
            client_threads_.emplace_back(&WsServer::ServeClient, this, client);
        }
        if (on_status_) on_status_(ClientCount());
    }
}

void WsServer::ServeClient(std::shared_ptr<WsClient> client) {
    const SOCKET socket = static_cast<SOCKET>(client->socket);

    std::string request;
    request.reserve(1024);
    size_t header_end = std::string::npos;
    while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
        char chunk[1024];
        const int received = recv(socket, chunk, sizeof chunk, 0);
        if (received <= 0) goto cleanup;
        request.append(chunk, static_cast<size_t>(received));
        if (request.size() > 65536) {
            SetError("handshake too large");
            goto cleanup;
        }
    }

    {
        const std::string lower = ToLower(request);
        const std::string needle = "sec-websocket-key:";
        const size_t key_position = lower.find(needle);
        if (key_position == std::string::npos) {
            SetError("handshake missing Sec-WebSocket-Key");
            goto cleanup;
        }
        const size_t value_begin = key_position + needle.size();
        const size_t value_end = request.find("\r\n", value_begin);
        const std::string key =
            Trim(request.substr(value_begin, value_end == std::string::npos
                                                ? std::string::npos
                                                : value_end - value_begin));
        const std::string accept = Sha1Base64(key + protocol::kWsGuid);
        const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Accept: " +
                                     accept + "\r\n\r\n";
        if (!SendExact(socket, response.data(), response.size())) goto cleanup;
    }

    SendFrame(*client, 0x1, "{\"t\":\"welcome\",\"v\":1,\"server\":\"streaming-enabled\"}");
    LogLine("client connected (%d total)", ClientCount());

    {
        std::string pending;
        for (;;) {
            unsigned char head[2];
            if (!RecvExact(socket, head, 2)) break;

            const bool fin = (head[0] & 0x80) != 0;
            const uint8_t opcode = static_cast<uint8_t>(head[0] & 0x0F);
            const bool masked = (head[1] & 0x80) != 0;
            uint64_t length = static_cast<uint64_t>(head[1] & 0x7F);

            if (length == 126) {
                unsigned char extended[2];
                if (!RecvExact(socket, extended, 2)) break;
                length = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
            } else if (length == 127) {
                unsigned char extended[8];
                if (!RecvExact(socket, extended, 8)) break;
                length = 0;
                for (int i = 0; i < 8; ++i) length = (length << 8) | extended[i];
            }

            if (length > (32ull << 20)) {
                SetError("frame too large");
                break;
            }

            unsigned char mask[4] = {0, 0, 0, 0};
            if (masked && !RecvExact(socket, mask, 4)) break;

            std::string payload;
            payload.resize(static_cast<size_t>(length));
            if (length > 0 && !RecvExact(socket, &payload[0], static_cast<size_t>(length))) break;
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i) {
                    payload[i] = static_cast<char>(payload[i] ^ mask[i & 3]);
                }
            }

            if (opcode == 0x8) {
                SendFrame(*client, 0x8, std::string());
                break;
            }
            if (opcode == 0x9) {
                SendFrame(*client, 0xA, payload);
                continue;
            }
            if (opcode == 0xA) continue;

            if (opcode == 0x0 || opcode == 0x1 || opcode == 0x2) {
                pending += payload;
                if (fin) {
                    if (on_text_ && !pending.empty()) on_text_(pending);
                    pending.clear();
                }
            }
        }
    }

cleanup:
    if (!client->closed.exchange(true)) {
        shutdown(socket, SD_BOTH);
        closesocket(socket);
    }
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), client), clients_.end());
    }
    LogLine("client disconnected (%d remaining)", ClientCount());
    if (on_status_) on_status_(ClientCount());
}

void WsServer::SendFrame(WsClient& client, uint8_t opcode, const std::string& payload) {
    if (client.closed) return;

    std::lock_guard<std::mutex> lock(client.send_mutex);
    std::string frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(static_cast<char>(0x80 | opcode));

    const size_t length = payload.size();
    if (length < 126) {
        frame.push_back(static_cast<char>(length));
    } else if (length <= 0xFFFF) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((length >> 8) & 0xFF));
        frame.push_back(static_cast<char>(length & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((static_cast<uint64_t>(length) >> (8 * i)) & 0xFF));
        }
    }
    frame += payload;

    const SOCKET socket = static_cast<SOCKET>(client.socket);
    if (socket == 0) return;
    SendExact(socket, frame.data(), frame.size());
}

void WsServer::Broadcast(const std::string& text) {
    std::vector<std::shared_ptr<WsClient>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }
    for (const auto& client : clients) {
        SendFrame(*client, 0x1, text);
    }
}

}  // namespace se
