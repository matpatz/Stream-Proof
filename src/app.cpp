#include "app.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include "log.h"

namespace se {
namespace {

using json = nlohmann::json;

const char* const kReservedKeys[] = {"id",  "name", "x",        "y",   "w",
                                     "h",   "health", "maxhealth", "distance", "color"};

bool IsReservedKey(const std::string& key) {
    for (const char* reserved : kReservedKeys) {
        if (key == reserved) return true;
    }
    return false;
}

bool ReadNumber(const json& object, const char* key, float* out) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return false;
    if (it->is_number()) {
        *out = it->get<float>();
        return true;
    }
    if (it->is_boolean()) {
        *out = it->get<bool>() ? 1.0f : 0.0f;
        return true;
    }
    if (it->is_string()) {
        try {
            *out = std::stof(it->get<std::string>());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

std::string ReadString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return std::string();
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number_unsigned()) return std::to_string(it->get<unsigned long long>());
    if (it->is_number_float()) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.2f", it->get<double>());
        return buffer;
    }
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    return std::string();
}

std::string ValueToString(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_number_float()) {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.1f", value.get<double>());
        return buffer;
    }
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return "-";

    std::string dumped = value.dump();
    if (dumped.size() > 48) {
        dumped.resize(45);
        dumped += "...";
    }
    return dumped;
}

bool HotkeyHeld(int virtual_key) { return (GetAsyncKeyState(virtual_key) & 0x8000) != 0; }

constexpr double kSnapshotStaleSeconds = 2.0;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Waits to a deadline without touching the system-wide timer resolution:
// timeBeginPeriod(1) applies to every process, so it would change the game's
// own scheduling just because an overlay is running.
void SleepUntilHighResolution(std::chrono::steady_clock::time_point target) {
    static const HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    const auto remaining = target - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) return;

    if (!timer) {
        std::this_thread::sleep_for(remaining);
        return;
    }

    LARGE_INTEGER due;
    // Positive = absolute time, negative = relative; 100ns units.
    due.QuadPart = -std::max<LONGLONG>(
        1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100);
    if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        std::this_thread::sleep_for(remaining);
        return;
    }
    WaitForSingleObject(timer, INFINITE);
}

}  // namespace

std::string DefaultLogPath() {
    wchar_t module_path[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    std::wstring path(module_path, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    path += L"streaming-enabled.log";

    const int needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
    if (needed > 1) {
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], needed, nullptr, nullptr);
    }
    return utf8;
}

void App::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    auto load_font = [](const char* path, float size) -> ImFont* {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size);
    };

    ImFont* body = load_font("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    ImFont* title = load_font("C:\\Windows\\Fonts\\segoeuib.ttf", 16.0f);
    if (!body && io.Fonts->Fonts.Size > 0) body = io.Fonts->Fonts[0];
    if (!body) body = io.Fonts->AddFontDefault();
    if (!title) title = body;
    overlay_.SetFonts(body, title);
}

void App::OnClientCountChange(int clients) {
    state_.SetConnected(clients > 0);
    if (clients == 0) {
        // Nothing is streaming any more: drop the last frame immediately so no
        // stale boxes stay on screen (or in a capture) after a disconnect.
        state_.SetSnapshot(Snapshot{});
    }
}

void App::OnText(const std::string& text) {
    state_.CountMessage(text.size());

    const json message = json::parse(text, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        LogLine("dropped malformed message (%zu bytes)", text.size());
        return;
    }

    const std::string type = message.value("t", std::string());

    if (type == "hello") {
        const std::string executor = message.value("executor", std::string("unknown"));
        const std::string game = message.value("game", std::string("unknown"));
        LogLine("hello from %s on %s", executor.c_str(), game.c_str());
        return;
    }

    if (type == "bye") {
        state_.SetSnapshot(Snapshot{});
        return;
    }

    if (type != "snapshot") return;

    const auto items = message.find("items");
    if (items == message.end() || !items->is_array()) return;

    Snapshot snapshot;
    snapshot.seq = message.value("seq", static_cast<uint64_t>(0));
    snapshot.received_at =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    snapshot.viewport_w = message.value("vw", 0.0f);
    snapshot.viewport_h = message.value("vh", 0.0f);
    snapshot.entities.reserve(items->size());

    size_t index = 0;
    for (const json& item : *items) {
        if (!item.is_object()) continue;

        Entity entity;
        entity.id = ReadString(item, "id");
        if (entity.id.empty()) entity.id = "entity-" + std::to_string(index);
        entity.name = ReadString(item, "name");
        entity.color = ReadString(item, "color");

        float value = 0.0f;
        if (ReadNumber(item, "x", &value)) entity.x = value;
        if (ReadNumber(item, "y", &value)) entity.y = value;

        const bool has_width = ReadNumber(item, "w", &value);
        entity.w = value;
        const bool has_height = ReadNumber(item, "h", &value);
        entity.h = value;
        entity.has_box = has_width && has_height && entity.w > 0.0f && entity.h > 0.0f;

        if (ReadNumber(item, "health", &value)) entity.health = value;
        if (ReadNumber(item, "maxhealth", &value)) entity.max_health = value;
        if (ReadNumber(item, "distance", &value)) entity.distance = value;

        for (auto it = item.begin(); it != item.end(); ++it) {
            if (IsReservedKey(it.key())) continue;
            if (entity.extras.size() >= 6) break;
            entity.extras.push_back({it.key(), ValueToString(it.value())});
        }

        snapshot.entities.push_back(std::move(entity));
        ++index;
    }

    if (!logged_viewport_) {
        logged_viewport_ = true;
        const double scale_x =
            snapshot.viewport_w > 1.0f ? display_.width() / snapshot.viewport_w : 1.0;
        const double scale_y =
            snapshot.viewport_h > 1.0f ? display_.height() / snapshot.viewport_h : 1.0;
        LogLine("first snapshot: game space %.0fx%.0f, overlay window %dx%d (scale %.4f x %.4f)",
                snapshot.viewport_w, snapshot.viewport_h, display_.width(), display_.height(),
                scale_x, scale_y);
    }

    const size_t entity_count = snapshot.entities.size();
    if (entity_count != last_logged_entity_count_) {
        last_logged_entity_count_ = entity_count;
        LogLine("snapshot: %zu entities", entity_count);
    }

    state_.SetSnapshot(std::move(snapshot));
}

int App::Run(const AppOptions& options) {
    options_ = options;

    DisplayConfig display_config;
    display_config.monitor = options_.monitor;
    display_config.width = options_.width;
    display_config.height = options_.height;
    display_config.game_process = options_.game_process;
    display_config.exclude_from_capture = options_.exclude_from_capture;
    display_config.click_through = options_.click_through;

    std::string error;
    if (!display_.Init(display_config, &error)) {
        LogLine("display init failed: %s", error.c_str());
        return 1;
    }
    display_.SetOpaqueDebug(options_.opaque_debug);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    LoadFonts();
    if (!ImGui_ImplDX11_Init(display_.device(), display_.context())) {
        LogLine("ImGui DX11 backend init failed");
        Shutdown();
        return 1;
    }

    if (!server_.Start(options_.port,
                       [this](const std::string& text) { OnText(text); },
                       [this](int clients) { OnClientCountChange(clients); })) {
        LogLine("websocket server failed: %s", server_.LastError().c_str());
        Shutdown();
        return 1;
    }

    LogLine("streaming enabled overlay ready (port %u, %d fps)", static_cast<unsigned>(server_.Port()),
            options_.fps);

    auto previous = std::chrono::steady_clock::now();
    auto next_frame = previous;
    float smoothed_fps = static_cast<float>(options_.fps);
    bool hide_hotkey_down = false;
    bool was_active = false;

    while (!display_.ShouldClose()) {
        display_.PollEvents();

        if (HotkeyHeld(VK_CONTROL) && HotkeyHeld(VK_MENU) && HotkeyHeld('Q')) break;

        if (HotkeyHeld(VK_CONTROL) && HotkeyHeld(VK_MENU) && HotkeyHeld('H') && !hide_hotkey_down) {
            hide_hotkey_down = true;
            display_.SetVisible(!display_.visible());
        } else if (!HotkeyHeld('H')) {
            hide_hotkey_down = false;
        }

        const auto frame_start = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(frame_start - previous).count();
        previous = frame_start;
        dt = std::clamp(dt, 0.0005f, 0.05f);

        const double now_seconds =
            std::chrono::duration<double>(frame_start.time_since_epoch()).count();
        const Snapshot snapshot = state_.GetSnapshot();
        const bool fresh = (now_seconds - snapshot.received_at) < kSnapshotStaleSeconds;
        const bool active = state_.Connected() && fresh && !snapshot.entities.empty();

        if (!active && was_active) {
            // The client left the game or died: wipe the frame now, so no marker
            // is left floating over the place you are back in.
            state_.SetSnapshot(Snapshot{});
            LogLine("stream stalled - clearing overlay");
        }
        // Render one extra (empty) frame on the way out to clear the screen.
        const bool draw_frame = active || was_active;
        was_active = active;

        if (draw_frame && display_.visible()) {
            smoothed_fps = smoothed_fps * 0.92f + (1.0f / dt) * 0.08f;
            io.DisplaySize = ImVec2(static_cast<float>(display_.width()),
                                    static_cast<float>(display_.height()));
            io.DeltaTime = dt;

            if (display_.BeginFrame()) {
                ImGui_ImplDX11_NewFrame();
                ImGui::NewFrame();

                OverlayStats stats;
                stats.fps = smoothed_fps;
                stats.clients = server_.ClientCount();
                stats.capture_excluded = display_.capture_excluded();
                stats.messages = state_.MessageCount();
                stats.bytes = state_.ByteCount();

                overlay_.Render(state_, stats, dt, display_.width(), display_.height());

                ImGui::Render();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                if (options_.diagnostics && ++diagnostics_counter_ >= 60) {
                    diagnostics_counter_ = 0;
                    const ImDrawData* draw_data = ImGui::GetDrawData();
                    LogLine("diagnostics: %zu non-transparent px, %d vertices, %zu entities",
                            display_.CountNonTransparentPixels(),
                            draw_data ? draw_data->TotalVtxCount : 0,
                            state_.GetSnapshot().entities.size());
                }

                display_.EndFrame();
            }
        }

        // Full speed only while markers are moving; idle at a crawl so the
        // compositor is not blending a full-screen layer 60 times a second for
        // nothing (that cost shows up as lost game FPS).
        const double target_fps = active ? std::max(1, options_.fps) : 5.0;
        next_frame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / target_fps));
        const auto now = std::chrono::steady_clock::now();
        if (next_frame < now) {
            next_frame = now;
        } else {
            SleepUntilHighResolution(next_frame);
        }
    }

    LogLine("shutting down");
    Shutdown();
    return 0;
}

void App::Shutdown() {
    server_.Stop();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
    }
    display_.Shutdown();
}

}  // namespace se
