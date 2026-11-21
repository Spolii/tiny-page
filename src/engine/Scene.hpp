#pragma once
#include <SDL3/SDL.h>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "core/UI.hpp"

struct Entity {
    std::string  name;
    float        x = 0, y = 0;   // keep x and y together: the Inspector edits them as a pair
    SDL_Texture* sprite = nullptr;
};

struct Scene {
    std::vector<Entity> entities;
    int selected = -1;

    void add(std::string name, float x, float y, SDL_Texture* sprite) {
        entities.push_back({std::move(name), x, y, sprite});
    }

    bool hasSelection() const { return selected >= 0 && selected < int(entities.size()); }

    void draw(SDL_Renderer* r) const {
        for (int i = 0; i < int(entities.size()); ++i) {
            const Entity& e = entities[i];
            float w = 16, h = 16;
            if (e.sprite) SDL_GetTextureSize(e.sprite, &w, &h);
            const SDL_FRect dst{std::round(e.x), std::round(e.y), w, h};
            if (e.sprite) SDL_RenderTexture(r, e.sprite, nullptr, &dst);
            if (i == selected) UI::outline(r, {dst.x - 1, dst.y - 1, w + 2, h + 2}, UI::accent);
        }
    }
};