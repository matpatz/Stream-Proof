#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace se {

struct FieldValue {
    std::string key;
    std::string value;
};

// One drawable thing. Everything except `id` is optional: an entity without a
// box is drawn as a text stack, which is how plain "player info" lists work.
struct Entity {
    std::string id;
    std::string name;
    std::string color;

    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool has_box = false;

    float health = -1.0f;
    float max_health = -1.0f;
    float distance = -1.0f;

    std::vector<FieldValue> extras;
};

struct Snapshot {
    std::vector<Entity> entities;
    uint64_t seq = 0;
    double received_at = 0.0;
    // Viewport the coordinates were measured in. The game reports viewport
    // pixels, which rarely match our window exactly, so the host scales by
    // window/viewport. Without this every marker drifts further off the more
    // it is from the top-left corner.
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
};

// Everything shared between the socket thread and the render thread.
class SharedState {
public:
    void SetSnapshot(Snapshot snapshot);
    Snapshot GetSnapshot() const;

    void SetConnected(bool connected);
    bool Connected() const;

    void CountMessage(size_t bytes);
    uint64_t MessageCount() const;
    uint64_t ByteCount() const;

private:
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    bool connected_ = false;
    uint64_t message_count_ = 0;
    uint64_t byte_count_ = 0;
};

}  // namespace se
