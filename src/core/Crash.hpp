#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include "Assets.hpp"
#include "Log.hpp"
#include "LogView.hpp"
#include "UI.hpp"
#include "Window.hpp"

inline void restartSelf() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    CreateProcessA(path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
}

// Call this yourself for any fatal error you detect, or let the handlers below call it.
inline void showCrashWindow(const std::string& reason) {
    static bool crashing = false;
    if (crashing) std::_Exit(1);   // crashed while crashing
    crashing = true;

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CRASH: %s", reason.c_str());
    Window::hideAll();

    const std::string logPath = std::string(SDL_GetBasePath()) + "crash.log";
    const std::string logText = Log::text();
    SDL_SaveFile(logPath.c_str(), logText.data(), logText.size());

    SDL_Surface* icon = SDL_LoadPNG(assetPath("icon.png").c_str());
    bool restart = false;
    {
        Window win("tiny-page - Crash Reporter", 760, 480, icon, false);
        SDL_Renderer* r = win.renderer;
        LogView      view(r);
        SDL_Texture* errorIcon = loadTexture(r, "error.png");
        bool open = true;

        while (open) {
            int iw, ih;
            SDL_GetRenderOutputSize(r, &iw, &ih);
            const float W = float(iw), H = float(ih);
            const SDL_FRect logArea{16, 116, W - 32, H - 116 - 64};
            const UI::Button buttons[3] = {
                {"Close",    {16,                     H - 48, 120, 32}},
                {"Copy log", {W - 16 - 120 - 8 - 120, H - 48, 120, 32}},
                {"Restart",  {W - 16 - 120,           H - 48, 120, 32}, true},
            };

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) open = false;
                if (e.type == SDL_EVENT_MOUSE_WHEEL) view.scroll += int(e.wheel.y) * 3;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (buttons[0].hit(e.button.x, e.button.y)) open = false;
                    if (buttons[1].hit(e.button.x, e.button.y)) SDL_SetClipboardText(logText.c_str());
                    if (buttons[2].hit(e.button.x, e.button.y)) { restart = true; open = false; }
                }
            }

            UI::clear(r, UI::bg);

            UI::fill(r, {0, 0, W, 56}, UI::panel);
            UI::fill(r, {0, 56, W, 1}, UI::border);
            SDL_FRect iconRect{16, 12, 32, 32};
            SDL_RenderTexture(r, errorIcon, nullptr, &iconRect);
            UI::label(r, 64, 20, "tiny-page has crashed", UI::fg, 2);

            UI::label(r, 16, 72, "Sorry about that. The engine can't continue, but the log below shows what happened.", UI::dim);
            UI::label(r, 16, 92, "Log saved to", UI::dim);
            UI::label(r, 16 + UI::textWidth("Log saved to "), 92, logPath.c_str(), UI::accent);

            UI::fill(r, logArea, UI::panel);
            view.draw(logArea, 1);
            UI::outline(r, logArea, UI::border);

            for (const UI::Button& b : buttons) b.draw(r);

            win.present();
            SDL_Delay(16);
        }
    }
    SDL_DestroySurface(icon);

    if (restart) restartSelf();
    std::_Exit(1);
}

inline void installCrashHandler() {
    // Uncaught C++ exceptions
    std::set_terminate([] {
        std::string reason = "terminate called";
        if (auto ex = std::current_exception()) {
            try { std::rethrow_exception(ex); }
            catch (const std::exception& e) { reason = e.what(); }
            catch (...)                     { reason = "unknown exception"; }
        }
        showCrashWindow(reason);
    });

    // abort() and failed assert()
    std::signal(SIGABRT, [](int) { showCrashWindow("abort() called"); });

    // Hard crashes (null pointer, access violation, ...)
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* p) -> LONG {
        char buf[64];
        std::snprintf(buf, sizeof buf, "Windows exception 0x%08lX", p->ExceptionRecord->ExceptionCode);
        showCrashWindow(buf);
        return EXCEPTION_EXECUTE_HANDLER;
    });
}