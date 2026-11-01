#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct ImFont;

namespace se {

class SharedState;

struct OverlayStats {
    float fps = 0.0f;
    int clients = 0;
    bool capture_excluded = false;
    uint64_t messages = 0;
    uint64_t bytes = 0;
};

// Draws whatever the latest snapshot describes. Purely a view: it never owns
// state that matters, so a dropped frame or a late packet is harmless.
class Overlay {
public:
    void SetFonts(ImFont* body, ImFont* title);
    void Render(const SharedState& state, const OverlayStats& stats, float dt, int width,
                int height);

private:
    struct Smooth {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool ready = false;
        double last_seen = 0.0;
    };

    ImFont* body_ = nullptr;
    ImFont* title_ = nullptr;
    std::unordered_map<std::string, Smooth> tracked_;
};

}  // namespace se
