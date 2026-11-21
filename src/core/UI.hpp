#pragma once
#include <SDL3/SDL.h>

// Shared look for every window + tiny drawing helpers.
namespace UI {

constexpr SDL_Color bg{30, 30, 40, 255}, panel{20, 20, 28, 255}, border{58, 58, 80, 255},
                    fg{220, 220, 230, 255}, dim{140, 140, 160, 255},
                    accent{66, 165, 245, 255}, accentHover{110, 190, 255, 255},
                    button{52, 52, 70, 255}, buttonHover{72, 72, 96, 255},
                    warn{255, 200, 80, 255}, error{255, 90, 90, 255};

inline void color(SDL_Renderer* r, SDL_Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

inline void clear(SDL_Renderer* r, SDL_Color c) { color(r, c); SDL_RenderClear(r); }

inline void fill(SDL_Renderer* r, SDL_FRect rect, SDL_Color c) {
    color(r, c);
    SDL_RenderFillRect(r, &rect);
}

inline void outline(SDL_Renderer* r, SDL_FRect rect, SDL_Color c) {
    color(r, c);
    SDL_RenderRect(r, &rect);
}

inline float textWidth(const char* s, float scale = 1) { return 8.0f * SDL_strlen(s) * scale; }

inline void label(SDL_Renderer* r, float x, float y, const char* s, SDL_Color c, float scale = 1) {
    color(r, c);
    SDL_SetRenderScale(r, scale, scale);
    SDL_RenderDebugText(r, x / scale, y / scale, s);
    SDL_SetRenderScale(r, 1, 1);
}

struct Button {
    const char* caption;
    SDL_FRect   rect;
    bool        primary = false;

    bool hit(float x, float y) const {
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    }

    void draw(SDL_Renderer* r) const {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        const bool hover = hit(mx, my);

        fill(r, rect, primary ? (hover ? accentHover : accent) : (hover ? buttonHover : button));
        if (!primary) outline(r, rect, border);
        label(r, rect.x + (rect.w - textWidth(caption)) / 2, rect.y + (rect.h - 8) / 2,
              caption, primary ? panel : fg);
    }
};

}  // namespace UI