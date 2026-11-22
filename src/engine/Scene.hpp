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
    float        x = 0, y = 0;    // the CENTER of the object; keep x and y together (Inspector edits them as a pair)
    float        rotation = 0;    // degrees, clockwise
    float        sx = 1, sy = 1;  // scale; keep sx and sy together
    std::string  sprite;          // asset name (see AssetStore), empty = none
    std::string  script;          // asset name of a .lua file, empty = none

    bool operator==(const Entity&) const = default;
};

struct Scene {
    std::vector<Entity> entities;
    std::vector<int>    selection;   // indices into `entities`, ascending
    int                 active = -1; // the one the Inspector shows (always inside `selection`)
    const AssetStore*   assets = nullptr;

    // ---- selection ----
    bool hasSelection() const { return !selection.empty(); }
    bool isSelected(int i) const { return std::binary_search(selection.begin(), selection.end(), i); }

    void select(int i) {
        selection.clear();
        active = -1;
        if (valid(i)) { selection.push_back(i); active = i; }
    }

    void toggle(int i) {
        if (!valid(i)) return;
        auto it = std::lower_bound(selection.begin(), selection.end(), i);
        if (it != selection.end() && *it == i) {
            selection.erase(it);
            if (active == i) active = selection.empty() ? -1 : selection.back();
        } else {
            selection.insert(it, i);
            active = i;
        }
    }

    void selectRange(int a, int b) {
        if (!valid(a)) { select(b); return; }
        selection.clear();
        for (int i = std::min(a, b); i <= std::max(a, b); ++i) selection.push_back(i);
        active = b;
    }

    void selectAll() {
        selection.clear();
        for (int i = 0; i < int(entities.size()); ++i) selection.push_back(i);
        active = selection.empty() ? -1 : selection.back();
    }

    // Replace the selection (duplicates and bad indices are dropped)
    void setSelection(std::vector<int> s, int prefer = -1) {
        std::sort(s.begin(), s.end());
        s.erase(std::unique(s.begin(), s.end()), s.end());
        std::erase_if(s, [&](int i) { return !valid(i); });
        selection = std::move(s);
        active = isSelected(prefer) ? prefer : (selection.empty() ? -1 : selection.back());
    }

    // ---- editing ----
    void add(std::string name, float x, float y, std::string sprite) {
        entities.push_back({std::move(name), x, y, 0.0f, 1.0f, 1.0f, std::move(sprite)});
    }

    void removeSelected() {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it)
            entities.erase(entities.begin() + *it);
        select(-1);
    }

    void duplicateSelected() {
        std::vector<int> created;
        for (int i : selection) {
            Entity copy = entities[i];
            copy.name += " Copy";
            copy.x += 8;
            copy.y += 8;
            entities.push_back(copy);
            created.push_back(int(entities.size()) - 1);
        }
        if (!created.empty()) setSelection(created, created.back());
    }

    // Entities are drawn in list order: the last one is on top
    void bringToFront() { reorder(true); }
    void sendToBack()   { reorder(false); }

    // Box in scene units, unrotated (position is the center)
    SDL_FRect bounds(const Entity& e) const {
        const SDL_FPoint s = assets ? assets->size(e.sprite) : SDL_FPoint{16, 16};
        const float w = s.x * e.sx, h = s.y * e.sy;
        return {std::round(e.x - w / 2), std::round(e.y - h / 2), w, h};
    }

    // Rotate a vector clockwise (y points down, like SDL)
    static SDL_FPoint rotate(float dx, float dy, float degrees) {
        const float a = degrees * std::numbers::pi_v<float> / 180.0f;
        const float c = std::cos(a), s = std::sin(a);
        return {dx * c - dy * s, dx * s + dy * c};
    }

    // Topmost entity under a scene-space point (respects rotation and scale), or -1
    int pick(float x, float y) const {
        for (int i = int(entities.size()) - 1; i >= 0; --i) {
            const SDL_FRect  b = bounds(entities[i]);
            const SDL_FPoint p = rotate(x - (b.x + b.w / 2), y - (b.y + b.h / 2), -entities[i].rotation);
            if (std::abs(p.x) <= b.w / 2 && std::abs(p.y) <= b.h / 2) return i;
        }
        return -1;
    }

    // Every entity that touches a scene-space rectangle
    std::vector<int> inRect(float x0, float y0, float x1, float y1) const {
        std::vector<int> hits;
        for (int i = 0; i < int(entities.size()); ++i) {
            const Entity&   e = entities[i];
            const SDL_FRect b = bounds(e);
            const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            for (float kx : {-0.5f, 0.5f})
                for (float ky : {-0.5f, 0.5f}) {
                    const SDL_FPoint q = rotate(kx * b.w, ky * b.h, e.rotation);
                    minX = std::min(minX, cx + q.x);  maxX = std::max(maxX, cx + q.x);
                    minY = std::min(minY, cy + q.y);  maxY = std::max(maxY, cy + q.y);
                }
            if (maxX >= x0 && minX <= x1 && maxY >= y0 && minY <= y1) hits.push_back(i);
        }
        return hits;
    }

    // `scale` = render pixels per scene unit
    void draw(SDL_Renderer* r, float scale) const {
        for (int i = 0; i < int(entities.size()); ++i) {
            const Entity&   e = entities[i];
            const SDL_FRect b = bounds(e);
            const SDL_FRect dst{b.x * scale, b.y * scale, b.w * scale, b.h * scale};
            SDL_Texture* tex = assets ? assets->texture(e.sprite) : nullptr;

            if (tex) SDL_RenderTextureRotated(r, tex, nullptr, &dst, e.rotation, nullptr, SDL_FLIP_NONE);
            else     quad(r, dst, e.rotation, 0, e.sprite.empty() ? UI::dim : UI::error);   // no sprite / missing asset

            if (isSelected(i)) {
                quad(r, dst, e.rotation, 1, UI::accent);
                quad(r, dst, e.rotation, 2, UI::accent);
            }
        }
    }

    // ---- play mode: the scene is saved on Play and restored on Stop ----
    bool isPlaying() const { return playing; }

    void beginPlay() {
        commitIfChanged();
        playBackup = entities;
        playing    = true;
    }

    void endPlay() {
        entities = std::move(playBackup);
        playBackup.clear();
        playing = false;
        setSelection(selection, active);
    }

    // ---- history (snapshots of `entities`) ----
    bool canUndo() const { return !playing && !undoStack.empty(); }
    bool canRedo() const { return !playing && !redoStack.empty(); }

    // Call once after building the initial scene
    void resetHistory() {
        baseline = entities;
        undoStack.clear();
        redoStack.clear();
    }

    // Call every frame while nothing is being dragged or typed
    void commitIfChanged() {
        if (playing || entities == baseline) return;
        undoStack.push_back(baseline);
        if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
        baseline = entities;
        redoStack.clear();
    }

    void undo() {
        if (playing) return;
        commitIfChanged();
        if (undoStack.empty()) return;
        redoStack.push_back(entities);
        entities = undoStack.back();
        undoStack.pop_back();
        baseline = entities;
        setSelection(selection, active);
    }

    void redo() {
        if (playing) return;
        commitIfChanged();
        if (redoStack.empty()) return;
        undoStack.push_back(entities);
        entities = redoStack.back();
        redoStack.pop_back();
        baseline = entities;
        setSelection(selection, active);
    }

private:
    bool valid(int i) const { return i >= 0 && i < int(entities.size()); }

    // Move the selected entities to the end (front) or the start (back), keeping their order
    void reorder(bool front) {
        if (!hasSelection()) return;
        std::vector<Entity> rest, picked;
        for (int i = 0; i < int(entities.size()); ++i)
            (isSelected(i) ? picked : rest).push_back(entities[i]);

        const int rank  = int(std::find(selection.begin(), selection.end(), active) - selection.begin());
        const int start = front ? int(rest.size()) : 0;

        std::vector<Entity> all = front ? rest : picked;
        const std::vector<Entity>& tail = front ? picked : rest;
        all.insert(all.end(), tail.begin(), tail.end());
        entities = std::move(all);

        selection.clear();
        for (int k = 0; k < int(picked.size()); ++k) selection.push_back(start + k);
        active = start + rank;
    }

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

    bool                             playing = false;
    std::vector<Entity>              playBackup;
    std::vector<Entity>              baseline;
    std::vector<std::vector<Entity>> undoStack, redoStack;
};