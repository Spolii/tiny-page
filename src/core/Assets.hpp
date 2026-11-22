#pragma once
#include <SDL3/SDL.h>
#include <string>

// The editor's own assets (icons), copied next to the exe
inline std::string assetPath(const char* name) {
    return std::string(SDL_GetBasePath()) + "assets/" + name;
}

inline SDL_Texture* loadTextureFile(SDL_Renderer* r, const std::string& path) {
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (!s) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(s);
    return t;
}

inline SDL_Texture* loadTexture(SDL_Renderer* r, const char* name) {
    return loadTextureFile(r, assetPath(name));
}