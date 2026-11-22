#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>
#include "AssetStore.hpp"
#include "core/UI.hpp"

struct Entity {
    std::string  name;
    float        x = 0, y = 0;   // keep x and y together: the Inspector edits them as a pair
    float        rotation = 0;   // degrees, clockwise
    std::string  sprite;         // asset name (see AssetStore), empty = none

    bool operator==(const Entity&) const = default;
};

struct Scene {
    std::vector<Entity> entities;
    int selected = -1;
    const AssetStore* assets = nullptr;   // where sprites come from

    // ---- editing ----
    void add(std::string name, float x, float y, std::string sprite) {
        entities.push_back({std::move(name), x, y, 0.0f, std::move(sprite)});
    }

    bool hasSelection() const { return selected >= 0 && selected < int(entities.size()); }

    void removeSelected() {
        if (!hasSelection()) return;
        entities.erase(entities.begin() + selected);
        selected = -1;
    }

    void duplicateSelected() {
        if (!hasSelection()) return;
        Entity copy = entities[selected];
        copy.name += " Copy";
        copy.x += 8;
        copy.y += 8;
        entities.push_back(copy);
        selected = int(entities.size()) - 1;
    }

    // Entities are drawn in list order: the last one is on top
    void bringToFront() {
        if (!hasSelection()) return;
        std::rotate(entities.begin() + selected, entities.begin() + selected + 1, entities.end());
        selected = int(entities.size()) - 1;
    }

    void sendToBack() {
        if (!hasSelection()) return;
        std::rotate(entities.begin(), entities.begin() + selected, entities.begin() + selected + 1);
        selected = 0;
    }

    // Unrotated box in scene units
    SDL_FRect bounds(const Entity& e) const {
        const SDL_FPoint s = assets ? assets->size(e.sprite) : SDL_FPoint{16, 16};
        return {std::round(e.x), std::round(e.y), s.x, s.y};
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
            const Entity&   e   = entities[i];
            const SDL_FRect b   = bounds(e);
            const SDL_FRect dst{b.x * scale, b.y * scale, b.w * scale, b.h * scale};
            SDL_Texture* tex = assets ? assets->texture(e.sprite) : nullptr;

            if (tex) SDL_RenderTextureRotated(r, tex, nullptr, &dst, e.rotation, nullptr, SDL_FLIP_NONE);
            else     quad(r, dst, e.rotation, 0, e.sprite.empty() ? UI::dim : UI::error);   // no sprite / missing asset

            if (i == selected) {
                quad(r, dst, e.rotation, 1, UI::accent);
                quad(r, dst, e.rotation, 2, UI::accent);
            }
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
    // A rotated rectangle outline, `grow` pixels outside the box
    static void quad(SDL_Renderer* r, const SDL_FRect& d, float degrees, float grow, SDL_Color c) {
        UI::color(r, c);
        const float cx = d.x + d.w / 2, cy = d.y + d.h / 2;
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

    void clampSelection() {
        if (selected >= int(entities.size())) selected = -1;
    }

    std::vector<Entity>              baseline;
    std::vector<std::vector<Entity>> undoStack, redoStack;
};