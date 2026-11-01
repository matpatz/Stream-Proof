#include "overlay.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "state.h"

namespace se {
namespace {

const ImU32 kPalette[] = {
    IM_COL32(94, 178, 255, 255),  IM_COL32(126, 240, 158, 255), IM_COL32(255, 184, 92, 255),
    IM_COL32(206, 140, 255, 255), IM_COL32(255, 120, 150, 255), IM_COL32(108, 232, 226, 255),
};

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Accepts "#RGB", "#RRGGBB" and "#RRGGBBAA"; anything else yields `fallback`.
ImU32 ParseColor(const std::string& text, ImU32 fallback) {
    if (text.size() < 4 || text[0] != '#') return fallback;

    unsigned values[4] = {0, 0, 0, 255};
    const size_t digits = text.size() - 1;
    if (digits == 3) {
        for (size_t i = 0; i < 3; ++i) {
            const int digit = HexDigit(text[1 + i]);
            if (digit < 0) return fallback;
            values[i] = static_cast<unsigned>(digit * 17);
        }
    } else if (digits == 6 || digits == 8) {
        for (size_t i = 0; i < digits / 2; ++i) {
            const int high = HexDigit(text[1 + i * 2]);
            const int low = HexDigit(text[2 + i * 2]);
            if (high < 0 || low < 0) return fallback;
            values[i] = static_cast<unsigned>(high * 16 + low);
        }
    } else {
        return fallback;
    }
    return IM_COL32(values[0], values[1], values[2], values[3]);
}

ImU32 WithAlpha(ImU32 color, int alpha) {
    alpha = std::clamp(alpha, 0, 255);
    return (color & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << 24);
}

ImU32 ScaleAlpha(ImU32 color, float factor) {
    const int alpha = static_cast<int>(((color >> 24) & 0xFF) * factor);
    return WithAlpha(color, alpha);
}

}  // namespace

void Overlay::SetFonts(ImFont* body, ImFont* title) {
    body_ = body;
    title_ = title;
}

void Overlay::Render(const SharedState& state, const OverlayStats& stats, float dt, int width,
                     int height) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Snapshot snapshot = state.GetSnapshot();
    const double now = ImGui::GetTime();

    ImFont* body = body_ ? body_ : ImGui::GetFont();
    ImFont* title = title_ ? title_ : body;
    const float body_size = body->FontSize;
    const float title_size = title->FontSize;

    // The game measures in its own viewport; our window is usually a slightly
    // different size, so map viewport pixels onto window pixels.
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    if (snapshot.viewport_w > 1.0f && width > 0) {
        scale_x = static_cast<float>(width) / snapshot.viewport_w;
    }
    if (snapshot.viewport_h > 1.0f && height > 0) {
        scale_y = static_cast<float>(height) / snapshot.viewport_h;
    }

    const float smooth = 1.0f - std::exp(-std::max(dt, 0.0f) * 22.0f);

    auto text_outlined = [draw](ImFont* font, float size, ImVec2 position, ImU32 color,
                                const std::string& text) {
        if (text.empty()) return;
        draw->AddText(font, size, ImVec2(position.x + 1.0f, position.y + 1.0f),
                      IM_COL32(0, 0, 0, 190), text.c_str());
        draw->AddText(font, size, position, color, text.c_str());
    };

    auto centered = [&](ImFont* font, float size, float center_x, float y, ImU32 color,
                        const std::string& text) {
        const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        text_outlined(font, size, ImVec2(center_x - extent.x * 0.5f, y), color, text);
    };

    for (size_t index = 0; index < snapshot.entities.size(); ++index) {
        const Entity& entity = snapshot.entities[index];
        const float target_x = entity.x * scale_x;
        const float target_y = entity.y * scale_y;
        const float target_w = entity.w * scale_x;
        const float target_h = entity.h * scale_y;

        Smooth& smooth_state = tracked_[entity.id];
        if (!smooth_state.ready) {
            smooth_state.x = target_x;
            smooth_state.y = target_y;
            smooth_state.w = target_w;
            smooth_state.h = target_h;
            smooth_state.ready = true;
        } else {
            smooth_state.x += (target_x - smooth_state.x) * smooth;
            smooth_state.y += (target_y - smooth_state.y) * smooth;
            smooth_state.w += (target_w - smooth_state.w) * smooth;
            smooth_state.h += (target_h - smooth_state.h) * smooth;
        }
        smooth_state.last_seen = now;

        const ImU32 color =
            ParseColor(entity.color, kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))]);
        const bool draw_box = entity.has_box && smooth_state.w >= 4.0f && smooth_state.h >= 4.0f;

        const float label_x = draw_box ? smooth_state.x + smooth_state.w * 0.5f : target_x;

        if (!draw_box) {
            float cursor_y = target_y;
            if (!entity.name.empty()) {
                text_outlined(title, title_size, ImVec2(target_x, cursor_y), color, entity.name);
                cursor_y += title_size + 2.0f;
            }
            if (entity.distance >= 0.0f) {
                char buffer[64];
                std::snprintf(buffer, sizeof buffer, "%.0fm", entity.distance);
                text_outlined(body, body_size, ImVec2(target_x, cursor_y),
                              IM_COL32(210, 216, 226, 220), buffer);
                cursor_y += body_size + 1.0f;
            }
            for (const FieldValue& field : entity.extras) {
                text_outlined(body, body_size, ImVec2(target_x, cursor_y),
                              IM_COL32(190, 198, 210, 210), field.key + ": " + field.value);
                cursor_y += body_size + 1.0f;
            }
            continue;
        }

        const ImVec2 top_left(smooth_state.x, smooth_state.y);
        const ImVec2 bottom_right(smooth_state.x + smooth_state.w, smooth_state.y + smooth_state.h);

        draw->AddRectFilled(top_left, bottom_right, WithAlpha(color, 22), 4.0f);
        draw->AddRect(top_left, bottom_right, ScaleAlpha(color, 0.55f), 4.0f, 0, 1.0f);

        const float arm = std::clamp(std::min(smooth_state.w, smooth_state.h) * 0.25f, 6.0f, 18.0f);
        const float thickness = 2.0f;
        draw->AddLine(top_left, ImVec2(top_left.x + arm, top_left.y), color, thickness);
        draw->AddLine(top_left, ImVec2(top_left.x, top_left.y + arm), color, thickness);
        draw->AddLine(ImVec2(bottom_right.x - arm, top_left.y), ImVec2(bottom_right.x, top_left.y),
                      color, thickness);
        draw->AddLine(ImVec2(bottom_right.x, top_left.y),
                      ImVec2(bottom_right.x, top_left.y + arm), color, thickness);
        draw->AddLine(ImVec2(top_left.x, bottom_right.y - arm),
                      ImVec2(top_left.x, bottom_right.y), color, thickness);
        draw->AddLine(ImVec2(top_left.x, bottom_right.y),
                      ImVec2(top_left.x + arm, bottom_right.y), color, thickness);
        draw->AddLine(ImVec2(bottom_right.x - arm, bottom_right.y), bottom_right, color, thickness);
        draw->AddLine(ImVec2(bottom_right.x, bottom_right.y - arm), bottom_right, color, thickness);

        if (entity.health >= 0.0f) {
            const float maximum = entity.max_health > 0.0f ? entity.max_health : 100.0f;
            const float fraction = std::clamp(entity.health / maximum, 0.0f, 1.0f);
            const ImVec2 bar_min(top_left.x - 7.0f, top_left.y);
            const ImVec2 bar_max(bar_min.x + 3.0f, bottom_right.y);
            draw->AddRectFilled(bar_min, bar_max, IM_COL32(0, 0, 0, 150), 2.0f);

            const ImU32 health_color = fraction > 0.6f   ? IM_COL32(96, 222, 128, 255)
                                       : fraction > 0.3f ? IM_COL32(240, 202, 84, 255)
                                                         : IM_COL32(236, 84, 84, 255);
            const float filled_top = bar_max.y - (bar_max.y - bar_min.y) * fraction;
            draw->AddRectFilled(ImVec2(bar_min.x, filled_top), bar_max, health_color, 2.0f);
        }

        if (!entity.name.empty()) {
            centered(title, title_size, label_x, top_left.y - title_size - 3.0f, color,
                     entity.name);
        }

        float cursor_y = bottom_right.y + 3.0f;
        if (entity.distance >= 0.0f) {
            char buffer[64];
            std::snprintf(buffer, sizeof buffer, "%.0fm", entity.distance);
            centered(body, body_size, label_x, cursor_y, IM_COL32(214, 220, 230, 230), buffer);
            cursor_y += body_size + 1.0f;
        }
        for (const FieldValue& field : entity.extras) {
            centered(body, body_size, label_x, cursor_y, IM_COL32(190, 198, 210, 215),
                     field.key + ": " + field.value);
            cursor_y += body_size + 1.0f;
        }
    }

    // Drop entities that stopped streaming so stale boxes never linger.
    for (auto it = tracked_.begin(); it != tracked_.end();) {
        if (now - it->second.last_seen > 2.0) {
            it = tracked_.erase(it);
        } else {
            ++it;
        }
    }

    const bool live = state.Connected();
    const ImU32 status_color = live ? IM_COL32(96, 222, 128, 255) : IM_COL32(240, 202, 84, 255);

    char header[128];
    std::snprintf(header, sizeof header, "%s   %zu players   %.0f fps",
                  live ? "LIVE" : "WAITING", snapshot.entities.size(), stats.fps);

    char detail[192];
    std::snprintf(detail, sizeof detail, "%d client(s)   %llu msgs   %.1f KB   capture: %s",
                  stats.clients, static_cast<unsigned long long>(stats.messages),
                  static_cast<double>(stats.bytes) / 1024.0,
                  stats.capture_excluded ? "excluded" : "VISIBLE TO OBS");

    const ImVec2 panel_min(16.0f, 16.0f);
    const ImVec2 header_size = title->CalcTextSizeA(title_size, FLT_MAX, 0.0f, header);
    const ImVec2 detail_size = body->CalcTextSizeA(body_size, FLT_MAX, 0.0f, detail);
    const float panel_width = std::max(header_size.x, detail_size.x) + 34.0f;
    const ImVec2 panel_max(panel_min.x + panel_width,
                           panel_min.y + header_size.y + detail_size.y + 20.0f);

    draw->AddRectFilled(panel_min, panel_max, IM_COL32(12, 15, 22, 190), 8.0f);
    draw->AddRect(panel_min, panel_max, IM_COL32(255, 255, 255, 26), 8.0f, 0, 1.0f);

    const ImVec2 dot(panel_min.x + 12.0f, panel_min.y + 11.0f + header_size.y * 0.5f);
    draw->AddCircleFilled(dot, 4.0f, status_color, 12);
    draw->AddCircle(dot, 7.0f, ScaleAlpha(status_color, 0.35f), 12, 1.0f);

    text_outlined(title, title_size, ImVec2(panel_min.x + 24.0f, panel_min.y + 10.0f),
                  IM_COL32(240, 244, 250, 245), header);
    text_outlined(body, body_size,
                  ImVec2(panel_min.x + 24.0f, panel_min.y + 12.0f + header_size.y),
                  IM_COL32(168, 178, 194, 235), detail);

    (void)width;
    (void)height;
}

}  // namespace se
