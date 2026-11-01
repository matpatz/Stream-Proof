#include "display.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cwchar>

#include "log.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

namespace se {
namespace {

// DirectComposition IIDs (the mingw headers only declare the interface uuid
// strings, so the GUIDs are spelled out here).
const GUID kIIDCompositionDevice = {
    0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
const GUID kIIDCompositionTarget = {
    0xeacdd04c, 0x117e, 0x4e17, {0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89}};
const GUID kIIDCompositionVisual = {
    0x4d93059d, 0x097b, 0x4651, {0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3}};

using SetWindowDisplayAffinityFn = BOOL(WINAPI*)(HWND, DWORD);

SetWindowDisplayAffinityFn ResolveDisplayAffinity() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return nullptr;
    return reinterpret_cast<SetWindowDisplayAffinityFn>(
        GetProcAddress(user32, "SetWindowDisplayAffinity"));
}

// One flag per call on purpose: Windows fails the whole SetWindowLongPtrW when a
// single flag is rejected (WS_EX_NOREDIRECTIONBITMAP only works at creation), and
// the failure is silent, so batching them drops the flags that would have worked.
bool TryAddExtendedStyle(HWND hwnd, LONG_PTR flag) {
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((current & flag) == flag) return true;

    SetLastError(0);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, current | flag);
    return GetLastError() == 0;
}

bool TryRemoveExtendedStyle(HWND hwnd, LONG_PTR flag) {
    const LONG_PTR current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((current & flag) == 0) return true;

    SetLastError(0);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, current & ~flag);
    return GetLastError() == 0;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return std::wstring();
    std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], needed);
    return wide;
}

struct WindowSearch {
    const std::wstring* executable;
    HWND found;
};

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (!process_id) return TRUE;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return TRUE;

    wchar_t image_path[MAX_PATH] = {0};
    DWORD length = MAX_PATH;
    bool matches = false;
    if (QueryFullProcessImageNameW(process, 0, image_path, &length)) {
        const std::wstring full(image_path, length);
        const size_t slash = full.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? full : full.substr(slash + 1);
        matches = _wcsicmp(name.c_str(), search->executable->c_str()) == 0;
    }
    CloseHandle(process);

    if (matches) {
        search->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindWindowOfProcess(const std::wstring& executable) {
    WindowSearch search{&executable, nullptr};
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

}  // namespace

Display::~Display() { Shutdown(); }

bool Display::Init(const DisplayConfig& config, std::string* error) {
    game_process_ = config.game_process;
    if (!SetProcessDPIAware()) {
        LogLine("SetProcessDPIAware failed (%lu), continuing", GetLastError());
    }

    glfwSetErrorCallback(
        [](int code, const char* description) { LogLine("glfw error %d: %s", code, description); });

    if (!glfwInit()) {
        if (error) *error = "glfwInit failed";
        return false;
    }
    glfw_ready_ = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    // Never let GLFW make it topmost: that is what put the overlay above every
    // other app. Stacking comes from owning the game window instead.
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

    GLFWmonitor* monitor = nullptr;
    int monitor_count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    if (config.monitor >= 0 && monitor_count > 0) {
        monitor = monitors[std::min(config.monitor, monitor_count - 1)];
    }
    if (!monitor) monitor = glfwGetPrimaryMonitor();

    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    width_ = config.width > 0 ? config.width : (mode ? mode->width : 1920);
    height_ = config.height > 0 ? config.height : (mode ? mode->height : 1080);

    GLFWwindow* window = glfwCreateWindow(width_, height_, "Streaming Enabled Overlay", nullptr,
                                          nullptr);
    if (!window) {
        if (error) *error = "glfwCreateWindow failed";
        Shutdown();
        return false;
    }
    window_ = window;

    int monitor_x = 0, monitor_y = 0;
    if (monitor) glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);

    // Sit exactly on the game's client area: the game reports viewport pixels,
    // so any offset here would shift every marker.
    placed_x_ = monitor_x;
    placed_y_ = monitor_y;
    if (!game_process_.empty()) {
        const std::wstring executable = Widen(game_process_);
        if (HWND game = FindWindowOfProcess(executable)) {
            RECT client = {};
            POINT origin = {0, 0};
            GetClientRect(game, &client);
            ClientToScreen(game, &origin);
            const int game_width = client.right - client.left;
            const int game_height = client.bottom - client.top;
            if (game_width > 0 && game_height > 0) {
                width_ = game_width;
                height_ = game_height;
                placed_x_ = origin.x;
                placed_y_ = origin.y;
                attached_ = true;
                game_hwnd_ = reinterpret_cast<uintptr_t>(game);
                glfwSetWindowSize(window, width_, height_);
            }
        } else {
            LogLine("game window '%s' not found, covering the monitor instead",
                    game_process_.c_str());
        }
    }
    glfwSetWindowPos(window, placed_x_, placed_y_);

    HWND hwnd = glfwGetWin32Window(window);
    hwnd_ = hwnd;

    TryAddExtendedStyle(hwnd, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    if (config.click_through) {
        // Both flags on purpose: TRANSPARENT alone passes plain left clicks, but
        // right-click and drag capture still get eaten unless the window is also
        // layered. Removing LAYERED is what broke right mouse.
        TryAddExtendedStyle(hwnd, WS_EX_LAYERED | WS_EX_TRANSPARENT);
    }
    // Only float above everything when we are not owned by the game; an owned
    // window already sits above its owner and stays under other apps.
    if (config.always_on_top && !attached_) {
        TryAddExtendedStyle(hwnd, WS_EX_TOPMOST);
    } else {
        TryRemoveExtendedStyle(hwnd, WS_EX_TOPMOST);
    }
    // Best effort only; this one is rejected outside CreateWindowEx.
    TryAddExtendedStyle(hwnd, WS_EX_NOREDIRECTIONBITMAP);

    if (attached_ && game_hwnd_) {
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, static_cast<LONG_PTR>(game_hwnd_));
    }

    const LONG_PTR applied = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool click_through = (applied & WS_EX_TRANSPARENT) != 0;
    const bool no_activate = (applied & WS_EX_NOACTIVATE) != 0;
    LogLine("extended styles 0x%016llx (click-through: %s, no-activate: %s)",
            static_cast<unsigned long long>(applied), click_through ? "yes" : "NO",
            no_activate ? "yes" : "NO");
    if (!click_through) LogLine("warning: the overlay window will swallow mouse input");
    if (!no_activate) LogLine("warning: the overlay window will take keyboard focus");

    SetWindowPos(hwnd, (config.always_on_top && !attached_) ? HWND_TOPMOST : HWND_TOP, placed_x_,
                 placed_y_, width_, height_,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (config.exclude_from_capture && !ExcludeFromCapture()) {
        LogLine("warning: this window is NOT excluded from screen capture");
    }

    if (!CreateDevice(error)) {
        Shutdown();
        return false;
    }
    if (!CreateSwapChain(error)) {
        Shutdown();
        return false;
    }
    if (!CreateComposition(error)) {
        Shutdown();
        return false;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    visible_ = true;
    UpdateGameVisibility(nullptr);
    LogLine("overlay window %dx%d on monitor %d (capture excluded: %s)", width_, height_,
            config.monitor, capture_excluded_ ? "yes" : "no");
    return true;
}

bool Display::ExcludeFromCapture() {
    const SetWindowDisplayAffinityFn set_affinity = ResolveDisplayAffinity();
    if (!set_affinity || !hwnd_) {
        LogLine("SetWindowDisplayAffinity unavailable");
        return false;
    }

    HWND hwnd = static_cast<HWND>(hwnd_);
    if (set_affinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        capture_excluded_ = true;
        LogLine("capture exclusion active (WDA_EXCLUDEFROMCAPTURE)");
        return true;
    }
    LogLine("WDA_EXCLUDEFROMCAPTURE failed (0x%08lx), trying WDA_MONITOR", GetLastError());
    if (set_affinity(hwnd, WDA_MONITOR)) {
        capture_excluded_ = true;
        LogLine("capture exclusion active (WDA_MONITOR)");
        return true;
    }
    LogLine("WDA_MONITOR failed (0x%08lx)", GetLastError());
    return false;
}

bool Display::CreateDevice(std::string* error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &feature_level,
                                   &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &feature_level,
                               &context_);
    }
    if (FAILED(hr)) {
        if (error) *error = "D3D11CreateDevice failed (0x" + std::to_string(hr) + ")";
        return false;
    }
    LogLine("d3d11 device created (feature level 0x%x)", feature_level);
    return true;
}

bool Display::CreateSwapChain(std::string* error) {
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;

    HRESULT hr = device_->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device));
    if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        if (error) *error = "IDXGIFactory2 unavailable";
        if (factory) factory->Release();
        if (adapter) adapter->Release();
        if (dxgi_device) dxgi_device->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    IDXGISwapChain1* swap_chain = nullptr;
    hr = factory->CreateSwapChainForComposition(device_, &desc, nullptr, &swap_chain);
    factory->Release();
    adapter->Release();
    dxgi_device->Release();

    if (FAILED(hr)) {
        if (error) *error = "CreateSwapChainForComposition failed (0x" + std::to_string(hr) + ")";
        return false;
    }
    swap_chain_ = swap_chain;

    ID3D11Texture2D* back_buffer = nullptr;
    hr = swap_chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&back_buffer));
    if (FAILED(hr)) {
        if (error) *error = "swap chain GetBuffer failed";
        return false;
    }
    hr = device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    back_buffer->Release();
    if (FAILED(hr)) {
        if (error) *error = "CreateRenderTargetView failed";
        return false;
    }
    return true;
}

bool Display::CreateComposition(std::string* error) {
    IDXGIDevice* dxgi_device = nullptr;
    HRESULT hr = device_->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) {
        if (error) *error = "QueryInterface(IDXGIDevice) failed";
        return false;
    }

    IDCompositionDevice* dcomp_device = nullptr;
    hr = DCompositionCreateDevice(dxgi_device, kIIDCompositionDevice,
                                  reinterpret_cast<void**>(&dcomp_device));
    dxgi_device->Release();
    if (FAILED(hr)) {
        if (error) *error = "DCompositionCreateDevice failed (0x" + std::to_string(hr) + ")";
        return false;
    }
    dcomp_device_ = dcomp_device;

    IDCompositionTarget* target = nullptr;
    hr = dcomp_device->CreateTargetForHwnd(static_cast<HWND>(hwnd_), TRUE, &target);
    if (FAILED(hr)) {
        if (error) *error = "CreateTargetForHwnd failed";
        return false;
    }
    dcomp_target_ = target;

    IDCompositionVisual* visual = nullptr;
    hr = dcomp_device->CreateVisual(&visual);
    if (FAILED(hr)) {
        if (error) *error = "CreateVisual failed";
        return false;
    }
    dcomp_visual_ = visual;

    hr = visual->SetContent(static_cast<IDXGISwapChain1*>(swap_chain_));
    if (SUCCEEDED(hr)) hr = target->SetRoot(visual);
    if (SUCCEEDED(hr)) hr = dcomp_device->Commit();
    if (FAILED(hr)) {
        if (error) *error = "composing the swap chain failed";
        return false;
    }
    return true;
}

// Follows the game window (move/resize/alt-tab) so markers keep matching.
void Display::SyncToGameWindow() {
    if (!window_ || game_process_.empty()) return;

    const uint64_t now = GetTickCount64();
    if (now - last_sync_ms_ < 500) return;
    last_sync_ms_ = now;

    const HWND game = FindWindowOfProcess(Widen(game_process_));
    if (!game) {
        if (attached_) {
            attached_ = false;
            LogLine("game window closed - keeping the last placement");
        }
        HideUntilAttached();
        return;
    }

    if (reinterpret_cast<uintptr_t>(game) != game_hwnd_) {
        game_hwnd_ = reinterpret_cast<uintptr_t>(game);
        SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_HWNDPARENT,
                          static_cast<LONG_PTR>(game_hwnd_));
        TryRemoveExtendedStyle(static_cast<HWND>(hwnd_), WS_EX_TOPMOST);
    }
    UpdateGameVisibility(game);

    RECT client = {};
    POINT origin = {0, 0};
    GetClientRect(game, &client);
    ClientToScreen(game, &origin);
    const int new_width = client.right - client.left;
    const int new_height = client.bottom - client.top;
    if (new_width <= 0 || new_height <= 0) return;
    if (attached_ && new_width == width_ && new_height == height_ && origin.x == placed_x_ &&
        origin.y == placed_y_) {
        return;
    }

    attached_ = true;
    placed_x_ = origin.x;
    placed_y_ = origin.y;
    SetWindowPos(static_cast<HWND>(hwnd_), nullptr, placed_x_, placed_y_, new_width, new_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Resize(new_width, new_height);
    LogLine("followed game window to %d,%d %dx%d", placed_x_, placed_y_, width_, height_);
}

// Only draw while the game is the app you are actually using: an owned,
// non-topmost window stacks above the game, and hiding it means it never shows
// up over Discord, a browser, or anything else.
void Display::UpdateGameVisibility(void* game_window) {
    (void)game_window;
    if (!hwnd_) return;

    // Ownership already keeps this above the game and below every other app, so
    // hiding on focus loss was pure downside: one missed focus event and the
    // overlay stayed invisible for the whole session.
    const bool should_show = game_process_.empty() || attached_;
    const bool currently_visible = IsWindowVisible(static_cast<HWND>(hwnd_)) != FALSE;
    if (should_show == currently_visible) return;

    ShowWindow(static_cast<HWND>(hwnd_), should_show ? SW_SHOWNOACTIVATE : SW_HIDE);
    LogLine("overlay %s", should_show ? "shown" : "hidden (no game window)");
}

// Without a known game window there is nothing to overlay, so stay hidden
// rather than floating a panel over whatever app you are actually using.
void Display::HideUntilAttached() {
    if (game_process_.empty() || attached_ || !hwnd_) return;
    ShowWindow(static_cast<HWND>(hwnd_), SW_HIDE);
}

bool Display::Resize(int width, int height) {
    if (width == width_ && height == height_ && render_target_) return true;
    width_ = width;
    height_ = height;

    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (!swap_chain_ || !device_) return true;

    auto* swap_chain = static_cast<IDXGISwapChain1*>(swap_chain_);
    const HRESULT result = swap_chain->ResizeBuffers(0, static_cast<UINT>(width),
                                                    static_cast<UINT>(height),
                                                    DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) {
        LogLine("ResizeBuffers failed (0x%08lx)", static_cast<unsigned long>(result));
        return false;
    }

    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(swap_chain->GetBuffer(0, IID_ID3D11Texture2D,
                                     reinterpret_cast<void**>(&back_buffer)))) {
        return false;
    }
    const HRESULT view_result =
        device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
    back_buffer->Release();
    return SUCCEEDED(view_result);
}

void Display::PollEvents() {
    glfwPollEvents();
    EnforceNoActivation();
    SyncToGameWindow();
}

// No-activate covers this, but some shell/input-stack combinations still hand the
// overlay focus; give it straight back so the game keeps receiving keystrokes.
void Display::EnforceNoActivation() {
    if (!hwnd_) return;

    const HWND overlay = static_cast<HWND>(hwnd_);
    const HWND foreground = GetForegroundWindow();
    if (foreground == overlay) {
        if (HWND previous = reinterpret_cast<HWND>(last_foreground_)) {
            SetForegroundWindow(previous);
        }
    } else if (foreground) {
        last_foreground_ = reinterpret_cast<uintptr_t>(foreground);
    }
}

bool Display::ShouldClose() const {
    return !window_ || glfwWindowShouldClose(static_cast<GLFWwindow*>(window_));
}

bool Display::BeginFrame() {
    if (!context_ || !render_target_) return false;
    const float background[4] = {opaque_debug_ ? 0.08f : 0.0f, opaque_debug_ ? 0.09f : 0.0f,
                                 opaque_debug_ ? 0.13f : 0.0f, opaque_debug_ ? 1.0f : 0.0f};
    context_->OMSetRenderTargets(1, &render_target_, nullptr);
    context_->ClearRenderTargetView(render_target_, background);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &viewport);
    return true;
}

void Display::EndFrame() {
    if (!swap_chain_) return;
    // No vsync: the app paces itself, and a composition swap chain is composited
    // by DWM at the desktop refresh rate regardless.
    const HRESULT result = static_cast<IDXGISwapChain1*>(swap_chain_)->Present(0, 0);
    if (FAILED(result) && result != last_present_result_) {
        LogLine("Present failed (0x%08lx)", static_cast<unsigned long>(result));
        last_present_result_ = result;
    }
}

size_t Display::CountNonTransparentPixels() {
    if (!swap_chain_ || !device_ || !context_) return 0;

    ID3D11Texture2D* back_buffer = nullptr;
    if (FAILED(static_cast<IDXGISwapChain1*>(swap_chain_)
                   ->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&back_buffer)))) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    back_buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) {
        back_buffer->Release();
        return 0;
    }
    context_->CopyResource(staging, back_buffer);
    back_buffer->Release();

    size_t non_transparent = 0;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        for (UINT y = 0; y < desc.Height; ++y) {
            const unsigned char* row =
                static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; ++x) {
                if (row[x * 4 + 3] != 0) ++non_transparent;
            }
        }
        context_->Unmap(staging, 0);
    }
    staging->Release();
    return non_transparent;
}

void Display::SetVisible(bool visible) {
    if (!window_ || visible == visible_) return;
    visible_ = visible;
    HWND hwnd = static_cast<HWND>(hwnd_);
    if (visible) {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }
}

void Display::ReleaseGraphics() {
    if (auto* visual = static_cast<IDCompositionVisual*>(dcomp_visual_)) {
        visual->Release();
        dcomp_visual_ = nullptr;
    }
    if (auto* target = static_cast<IDCompositionTarget*>(dcomp_target_)) {
        target->Release();
        dcomp_target_ = nullptr;
    }
    if (auto* dcomp = static_cast<IDCompositionDevice*>(dcomp_device_)) {
        dcomp->Release();
        dcomp_device_ = nullptr;
    }
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (auto* swap_chain = static_cast<IDXGISwapChain1*>(swap_chain_)) {
        swap_chain->Release();
        swap_chain_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void Display::Shutdown() {
    ReleaseGraphics();
    if (window_) {
        glfwDestroyWindow(static_cast<GLFWwindow*>(window_));
        window_ = nullptr;
        hwnd_ = nullptr;
    }
    if (glfw_ready_) {
        glfwTerminate();
        glfw_ready_ = false;
    }
}

}  // namespace se
