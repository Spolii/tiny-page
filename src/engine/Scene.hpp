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
    float        rotation = 0;   // degrees
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

    static SDL_FRect bounds(const Entity& e) {
        float w = 16, h = 16;
        if (e.sprite) SDL_GetTextureSize(e.sprite, &w, &h);
        return {std::round(e.x), std::round(e.y), w, h};
    }

    // Topmost entity under a scene-space point, or -1
    int pick(float x, float y) const {
        for (int i = int(entities.size()) - 1; i >= 0; --i) {
            const SDL_FRect b = bounds(entities[i]);
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
        }
        return -1;
    }

    void draw(SDL_Renderer* r) const {
        for (int i = 0; i < int(entities.size()); ++i) {
            const Entity&   e   = entities[i];
            const SDL_FRect dst = bounds(e);
            if (e.sprite) SDL_RenderTextureRotated(r, e.sprite, nullptr, &dst, e.rotation, nullptr, SDL_FLIP_NONE);
            if (i == selected) UI::outline(r, {dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2}, UI::accent);
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
    void clampSelection() {
        if (selected >= int(entities.size())) selected = -1;
    }

    std::vector<Entity>              baseline;
    std::vector<std::vector<Entity>> undoStack, redoStack;
};