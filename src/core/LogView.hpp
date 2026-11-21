#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <string>
#include <vector>
#include "Assets.hpp"
#include "Log.hpp"
#include "UI.hpp"

// Draws the log (icon + colored, word-wrapped text) into an area of any renderer.
class LogView {
public:
    int scroll = 0;   // how many rows above the newest line

    explicit LogView(SDL_Renderer* r) : renderer(r) {
        icons[0] = loadTexture(r, "info.png");
        icons[1] = loadTexture(r, "warning.png");
        icons[2] = loadTexture(r, "error.png");
    }

    void draw() {
        int w, h;
        SDL_GetRenderOutputSize(renderer, &w, &h);
        draw({0, 0, float(w), float(h)}, 2);
    }

    void draw(SDL_FRect area, float scale) {
        struct Row { std::string text; int level; bool first; };

        const int cols = std::max(1, int((area.w / scale - 28) / 8));
        std::vector<Row> rows;
        for (const auto& e : Log::entries()) {
            const int k = level(e.level);
            size_t i = 0;
            do {
                rows.push_back({e.text.substr(i, cols), k, i == 0});
                i += cols;
            } while (i < e.text.size());
        }

        const int fit   = std::max(1, int(area.h / scale / rowH));
        scroll          = std::clamp(scroll, 0, std::max(0, int(rows.size()) - fit));
        const int from  = std::max(0, int(rows.size()) - fit - scroll);
        const int count = std::min(fit, int(rows.size()) - from);

        SDL_SetRenderScale(renderer, scale, scale);
        for (int i = 0; i < count; ++i) {
            const Row& row = rows[from + i];
            const float x = area.x / scale;
            const float y = area.y / scale + i * rowH;
            if (row.first) {
                SDL_FRect dst{x + 4, y + 1, 16, 16};
                SDL_RenderTexture(renderer, icons[row.level], nullptr, &dst);
            }
            UI::color(renderer, colors[row.level]);
            SDL_RenderDebugText(renderer, x + 24, y + 5, row.text.c_str());
        }
        SDL_SetRenderScale(renderer, 1, 1);
    }

private:
    static constexpr float rowH = 18;

    static int level(SDL_LogPriority p) {
        if (p >= SDL_LOG_PRIORITY_ERROR) return 2;
        if (p == SDL_LOG_PRIORITY_WARN)  return 1;
        return 0;
    }

    SDL_Renderer* renderer;
    SDL_Texture*  icons[3];
    SDL_Color     colors[3] = {UI::fg, UI::warn, UI::error};
};