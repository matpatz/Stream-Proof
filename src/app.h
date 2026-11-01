#pragma once

#include <cstdint>
#include <string>

#include "display.h"
#include "overlay.h"
#include "state.h"
#include "ws_server.h"

namespace se {

struct AppOptions {
    uint16_t port = 34891;
    int fps = 60;
    int monitor = -1;
    int width = 0;
    int height = 0;
    std::string game_process = "RobloxPlayerBeta.exe";
    bool exclude_from_capture = true;
    bool click_through = true;
    bool diagnostics = false;
    bool opaque_debug = false;
};

class App {
public:
    int Run(const AppOptions& options);

private:
    void LoadFonts();
    void OnText(const std::string& text);
    void OnClientCountChange(int clients);
    void Shutdown();

    AppOptions options_;
    Display display_;
    WsServer server_;
    SharedState state_;
    Overlay overlay_;
    int diagnostics_counter_ = 0;
    size_t last_logged_entity_count_ = 0;
    bool logged_viewport_ = false;
};

// Log next to the executable so a window-less build is still debuggable.
std::string DefaultLogPath();

}  // namespace se
