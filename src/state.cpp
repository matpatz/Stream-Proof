#include "state.h"

#include <utility>

namespace se {

void SharedState::SetSnapshot(Snapshot snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snapshot);
}

Snapshot SharedState::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void SharedState::SetConnected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = connected;
}

bool SharedState::Connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void SharedState::CountMessage(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++message_count_;
    byte_count_ += bytes;
}

uint64_t SharedState::MessageCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return message_count_;
}

uint64_t SharedState::ByteCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return byte_count_;
}

}  // namespace se
