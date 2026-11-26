#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <memory>
#include "core/Assets.hpp"
#include "core/Crash.hpp"
#include "core/Log.hpp"
#include "core/Window.hpp"
#include "engine/Editor.hpp"

int main(int, char**) {
    Log::init();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    installCrashHandler();

    SDL_Surface* icon = SDL_LoadPNG(assetPath("icon.png").c_str());
    auto mainWindow = std::make_unique<Window>("tiny-page", 1280, 720, icon);
    auto editor     = std::make_unique<Editor>(*mainWindow);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Engine started");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            editor->handleEvent(e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) editor->requestQuit();
        }
        if (editor->wantsQuit) running = false;
        editor->frame();
    }

    editor.reset();       // must go before the window
    mainWindow.reset();
    SDL_DestroySurface(icon);
    SDL_Quit();
    return 0;
}