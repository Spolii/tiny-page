#pragma once
#include <SDL3/SDL.h>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include "core/UI.hpp"

struct Entity {
    std::string  name;
    float        x = 0, y = 0;   // keep x and y together: the Inspector edits them as a pair
    float        rotation = 0;   // degrees, clockwise
    SDL_Texture* sprite = nullptr;

    bool operator==(const Entity&) const = default;
};

struct Scene {
    std::vector<Entity> entities;
    int selected = -1;

    // ---- editing ----
    void add(std::string name, float x, float y, SDL_Texture* sprite) {
        entities.push_back({std::move(name), x, y, 0.0f, sprite});
    }

    bool hasSelection() const { return selected >= 0 && selected < int(entities.size()); }

    void removeSelected() {
        if (!hasSelection()) return;
        entities.erase(entities.begin() + selected);
        selected = -1;
    }

    // Unrotated box in scene units
    static SDL_FRect bounds(const Entity& e) {
        float w = 16, h = 16;
        if (e.sprite) SDL_GetTextureSize(e.sprite, &w, &h);
        return {std::round(e.x), std::round(e.y), w, h};
    }

    // Rotate a vector clockwise (y points down, like SDL)
    static SDL_FPoint rotate(float dx, float dy, float degrees) {
        const float a = degrees * std::numbers::pi_v<float> / 180.0f;
        const float c = std::cos(a), s = std::sin(a);
        return {dx * c - dy * s, dx * s + dy * c};
    }

    // Topmost entity under a scene-space point (respects rotation), or -1
    int pick(float x, float y) const {
        for (int i = int(entities.size()) - 1; i >= 0; --i) {
            const SDL_FRect  b = bounds(entities[i]);
            const SDL_FPoint p = rotate(x - (b.x + b.w / 2), y - (b.y + b.h / 2), -entities[i].rotation);
            if (std::abs(p.x) <= b.w / 2 && std::abs(p.y) <= b.h / 2) return i;
        }
        return -1;
    }

    // `scale` = render pixels per scene unit
    void draw(SDL_Renderer* r, float scale) const {
        for (int i = 0; i < int(entities.size()); ++i) {
            const Entity&   e = entities[i];
            const SDL_FRect b = bounds(e);
            const SDL_FRect dst{b.x * scale, b.y * scale, b.w * scale, b.h * scale};
            if (e.sprite) SDL_RenderTextureRotated(r, e.sprite, nullptr, &dst, e.rotation, nullptr, SDL_FLIP_NONE);
            if (i == selected) outline(r, dst, e.rotation);
        }
    }

    // ---- history (snapshots of `entities`) ----
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }

    // Call once after building the initial scene
    void resetHistory() {
        baseline = entities;
        undoStack.clear();
        redoStack.clear();
    }

    // Call every frame while nothing is being dragged or typed
    void commitIfChanged() {
        if (entities == baseline) return;
        undoStack.push_back(baseline);
        if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
        baseline = entities;
        redoStack.clear();
    }

    void undo() {
        commitIfChanged();
        if (undoStack.empty()) return;
        redoStack.push_back(entities);
        entities = undoStack.back();
        undoStack.pop_back();
        baseline = entities;
        clampSelection();
    }

    void redo() {
        commitIfChanged();
        if (redoStack.empty()) return;
        undoStack.push_back(entities);
        entities = redoStack.back();
        redoStack.pop_back();
        baseline = entities;
        clampSelection();
    }

private:
    // Rotated selection box, 2 pixels thick
    static void outline(SDL_Renderer* r, const SDL_FRect& d, float degrees) {
        UI::color(r, UI::accent);
        const float cx = d.x + d.w / 2, cy = d.y + d.h / 2;
        for (float grow : {1.0f, 2.0f}) {
            const float hw = d.w / 2 + grow, hh = d.h / 2 + grow;
            const SDL_FPoint corners[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
            SDL_FPoint pts[5];
            for (int k = 0; k < 4; ++k) {
                const SDL_FPoint q = rotate(corners[k].x, corners[k].y, degrees);
                pts[k] = {cx + q.x, cy + q.y};
            }
            pts[4] = pts[0];
            SDL_RenderLines(r, pts, 5);
        }
    }

    void clampSelection() {
        if (selected >= int(entities.size())) selected = -1;
    }

    std::vector<Entity>              baseline;
    std::vector<std::vector<Entity>> undoStack, redoStack;
};