#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>

// One OS window + its renderer. Every window registers itself in Window::all.
class Window {
public:
    static inline std::vector<Window*> all;

    Window(const std::string& title, int w, int h, SDL_Surface* icon = nullptr, bool resizable = true) {
        window   = SDL_CreateWindow(title.c_str(), w, h, resizable ? SDL_WINDOW_RESIZABLE : 0);
        renderer = SDL_CreateRenderer(window, nullptr);
        if (icon) SDL_SetWindowIcon(window, icon);
        all.push_back(this);
    }
    ~Window() {
        std::erase(all, this);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    static void hideAll() {
        for (Window* w : all) SDL_HideWindow(w->window);
    }

    SDL_WindowID id() const { return SDL_GetWindowID(window); }

    void clear(Uint8 r, Uint8 g, Uint8 b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderClear(renderer);
    }
    void present() { SDL_RenderPresent(renderer); }

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
};