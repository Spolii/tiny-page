#pragma once
#include <SDL3/SDL.h>
#include <string>

inline std::string assetPath(const char* name) {
    return std::string(SDL_GetBasePath()) + "assets/" + name;
}

inline SDL_Texture* loadTexture(SDL_Renderer* r, const char* name) {
    SDL_Surface* s = SDL_LoadPNG(assetPath(name).c_str());
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(s);
    return t;
}