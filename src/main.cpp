#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app.h"
#include "log.h"
#include "protocol.h"

namespace {

void PrintUsage() {
    std::printf(
        "Streaming Enabled - drawings the game never renders, so OBS never sees them.\n\n"
        "Usage: StreamingEnabled.exe [options]\n"
        "  --port <n>        WebSocket port to listen on (default %u)\n"
        "  --fps <n>         Overlay refresh rate (default 60)\n"
        "  --monitor <n>     Monitor index to cover (default: primary)\n"
        "  --width <n>       Window width override (default: monitor width)\n"
        "  --height <n>      Window height override (default: monitor height)\n"
        "  --game <exe>      Follow this process' window (default RobloxPlayerBeta.exe)\n"
        "  --no-attach       Cover a monitor instead of following the game window\n"
        "  --no-exclude      Do not exclude the overlay from screen capture\n"
        "  --interactive     Let the window receive mouse input (not click-through)\n"
        "  --console         Attach a console for live logs\n"
        "  --help            Show this message\n\n"
        "Hotkeys: Ctrl+Alt+H toggles the overlay, Ctrl+Alt+Q quits.\n",
        static_cast<unsigned>(se::protocol::kDefaultPort));
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    se::AppOptions options;
    options.port = se::protocol::kDefaultPort;
    bool use_console = false;
    bool want_exit = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next_value = [&](int& out) {
            if (i + 1 >= argc) return false;
            out = std::atoi(argv[++i]);
            return true;
        };

        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            want_exit = true;
        } else if (std::strcmp(arg, "--port") == 0) {
            int port = 0;
            if (next_value(port) && port > 0 && port < 65536) options.port = static_cast<uint16_t>(port);
        } else if (std::strcmp(arg, "--fps") == 0) {
            int fps = 0;
            if (next_value(fps) && fps > 0 && fps <= 1000) options.fps = fps;
        } else if (std::strcmp(arg, "--monitor") == 0) {
            int monitor = 0;
            if (next_value(monitor) && monitor >= 0) options.monitor = monitor;
        } else if (std::strcmp(arg, "--width") == 0) {
            int value = 0;
            if (next_value(value) && value > 0) options.width = value;
        } else if (std::strcmp(arg, "--height") == 0) {
            int value = 0;
            if (next_value(value) && value > 0) options.height = value;
        } else if (std::strcmp(arg, "--game") == 0) {
            if (i + 1 < argc) options.game_process = argv[++i];
        } else if (std::strcmp(arg, "--no-attach") == 0) {
            options.game_process.clear();
        } else if (std::strcmp(arg, "--no-exclude") == 0) {
            options.exclude_from_capture = false;
        } else if (std::strcmp(arg, "--interactive") == 0) {
            options.click_through = false;
        } else if (std::strcmp(arg, "--console") == 0) {
            use_console = true;
        } else if (std::strcmp(arg, "--diagnostics") == 0) {
            options.diagnostics = true;
        } else if (std::strcmp(arg, "--opaque") == 0) {
            options.opaque_debug = true;
        }
    }

    if (want_exit) {
        se::LogInit(se::DefaultLogPath(), true);
        PrintUsage();
        se::LogShutdown();
        return 0;
    }

    se::LogInit(se::DefaultLogPath(), use_console);
    se::LogLine("streaming enabled starting (build %s %s)", __DATE__, __TIME__);

    se::App app;
    const int result = app.Run(options);

    se::LogLine("exiting with code %d", result);
    se::LogShutdown();
    return result;
}
