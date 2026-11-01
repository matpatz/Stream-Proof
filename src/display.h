#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace se {

struct DisplayConfig {
    int monitor = -1;               // -1 = primary monitor
    int width = 0;                  // 0 = monitor width
    int height = 0;                 // 0 = monitor height
    std::string game_process;       // attach to this process' window; empty = monitor
    bool exclude_from_capture = true;
    bool click_through = true;
    bool always_on_top = true;
};

// A borderless, per-pixel transparent, click-through window over the game,
// backed by a D3D11 composition swapchain through DirectComposition.
class Display {
public:
    Display() = default;
    ~Display();

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    bool Init(const DisplayConfig& config, std::string* error);
    void Shutdown();

    void PollEvents();
    bool ShouldClose() const;

    bool BeginFrame();
    void EndFrame();

    // Diagnostics: how many pixels of the frame that is about to be presented
    // carry any alpha at all. Used to tell "nothing is drawn" apart from
    // "drawn but not composited".
    size_t CountNonTransparentPixels();

    void SetOpaqueDebug(bool opaque) { opaque_debug_ = opaque; }

    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

    int width() const { return width_; }
    int height() const { return height_; }
    bool attached_to_game() const { return attached_; }
    bool capture_excluded() const { return capture_excluded_; }
    bool visible() const { return visible_; }
    void SetVisible(bool visible);

private:
    void EnforceNoActivation();
    void SyncToGameWindow();
    void UpdateGameVisibility(void* game_window);
    void HideUntilAttached();
    bool Resize(int width, int height);
    bool CreateDevice(std::string* error);
    bool CreateSwapChain(std::string* error);
    bool CreateComposition(std::string* error);
    bool ExcludeFromCapture();
    void ReleaseGraphics();

    void* window_ = nullptr;  // GLFWwindow*
    void* hwnd_ = nullptr;    // HWND

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    void* swap_chain_ = nullptr;  // IDXGISwapChain1*
    ID3D11RenderTargetView* render_target_ = nullptr;
    void* dcomp_device_ = nullptr;
    void* dcomp_target_ = nullptr;
    void* dcomp_visual_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    std::string game_process_;
    int placed_x_ = 0;
    int placed_y_ = 0;
    bool attached_ = false;
    uintptr_t game_hwnd_ = 0;
    uint64_t last_sync_ms_ = 0;
    bool capture_excluded_ = false;
    bool visible_ = false;
    bool glfw_ready_ = false;
    bool opaque_debug_ = false;
    long last_present_result_ = 0;
    uintptr_t last_foreground_ = 0;
};

}  // namespace se
