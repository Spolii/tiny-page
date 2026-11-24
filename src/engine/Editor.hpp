#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "AssetStore.hpp"
#include "Scene.hpp"
#include "Scripting.hpp"
#include "core/Assets.hpp"
#include "core/Log.hpp"
#include "core/UI.hpp"
#include "core/Window.hpp"

// The main window: menu bar, Hierarchy, Scene, Inspector and a bottom Console/Assets panel.
// Create it after its Window and destroy it before the Window.
class Editor {
public:
    bool wantsQuit = false;

    explicit Editor(Window& w) : win(w), r(w.renderer), assets(w.renderer) {
        SDL_SetRenderVSync(r, 1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        applyStyle();
        ImGui_ImplSDL3_InitForSDLRenderer(w.window, r);
        ImGui_ImplSDLRenderer3_Init(r);

        logIcons[0] = loadTexture(r, "info.png");
        logIcons[1] = loadTexture(r, "warning.png");
        logIcons[2] = loadTexture(r, "error.png");

        // Demo objects, for whichever of these assets exist in project/assets
        scene.assets = &assets;
        const struct { const char* name; const char* sprite; float x, y; } demo[] = {
            {"Icon", "icon.png", 48, 48}, {"Info", "info.png", 128, 68},
            {"Warning", "warning.png", 208, 98}, {"Error", "error.png", 128, 138},
        };
        for (const auto& d : demo)
            if (assets.find(d.sprite)) scene.add(d.name, d.x, d.y, d.sprite);
        scene.resetHistory();
    }

    ~Editor() {
        scripting.stop();
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void handleEvent(const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_DROP_FILE && assets.importFile(e.drop.data)) refreshAssets = true;
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) refreshAssets = true;
    }

    void frame() {
        // Safe moment to change textures: nothing from this frame references them yet
        if (!deleteAsset.empty()) {
            assets.remove(deleteAsset);
            deleteAsset.clear();
            selectedAsset.clear();
            refreshAssets = true;
        }
        if (refreshAssets) {
            assets.refresh();
            refreshAssets = false;
        }

        // Run the scripts
        const Uint64 now = SDL_GetTicksNS();
        const float  dt  = lastTicks ? std::min(float(now - lastTicks) / 1e9f, 0.1f) : 0.0f;
        lastTicks = now;
        if (mode == Mode::Playing) scripting.update(scene, dt);

        assetsFocusedLast = assetsFocused;   // so Delete knows what you clicked last
        assetsFocused     = false;

        // 1. Build the UI
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        shortcuts();
        menuBar();

        const ImVec2 d      = ImGui::GetIO().DisplaySize;
        const float  top    = ImGui::GetFrameHeight();
        const float  left   = 220, right = 280;
        const float  bottom = std::min(220.0f, d.y * 0.35f);
        const float  midW   = d.x - left - right;
        const float  midH   = d.y - top - bottom;

        hierarchy(0, top, left, midH);
        sceneView(left, top, midW, midH);
        inspector(left + midW, top, right, midH);
        bottomPanel(0, top + midH, d.x, bottom);
        deleteAssetDialog();

        // One finished drag / rename / action = one undo step (not recorded while playing)
        if (!ImGui::IsAnyItemActive()) scene.commitIfChanged();

        ImGui::Render();

        // 2. Draw the scene at the size it is shown (after the UI, so edits show immediately)
        if (view) {
            SDL_SetRenderTarget(r, view);
            UI::clear(r, {24, 24, 34, 255});
            scene.draw(r, float(viewScale));
            SDL_SetRenderTarget(r, nullptr);
        }

        // 3. Draw the UI on top
        UI::clear(r, UI::bg);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), r);
        win.present();
    }

private:
    enum class Mode { Stopped, Playing, Paused };
    enum class Drag { None, Move, Marquee };
    enum class Icon { Play, Pause, Stop, Lock, Unlock };

    static constexpr int viewW = 320, viewH = 180;   // scene size in scene units

    // Context menus: a popup's mouse button is part of its flags and 0 means LEFT,
    // so every menu names the right button explicitly.
    static constexpr ImGuiPopupFlags rightClick      = ImGuiPopupFlags_MouseButtonRight;
    static constexpr ImGuiPopupFlags rightClickEmpty = ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems;

    static ImVec4 col(SDL_Color c, float a = 1) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a}; }
    static ImTextureID tid(SDL_Texture* t) { return (ImTextureID)(intptr_t)t; }

    static bool panel(const char* name, float x, float y, float w, float h, ImGuiWindowFlags extra = 0) {
        ImGui::SetNextWindowPos({x, y});
        ImGui::SetNextWindowSize({w, h});
        return ImGui::Begin(name, nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
    }

    // Draggable number fields: show a "drag sideways" cursor, return true when the value changed
    static void dragCursor() {
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    static bool dragFloat(const char* label, float* v, float speed = 1, float min = 0, float max = 0,
                          const char* fmt = "%.1f") {
        const bool changed = ImGui::DragFloat(label, v, speed, min, max, fmt);
        dragCursor();
        return changed;
    }
    static bool dragFloat2(const char* label, float* v, float speed = 1, float min = 0, float max = 0,
                           const char* fmt = "%.1f") {
        const bool changed = ImGui::DragFloat2(label, v, speed, min, max, fmt);
        dragCursor();
        return changed;
    }
    static bool dragInt(const char* label, int* v, float speed = 0.2f) {
        const bool changed = ImGui::DragInt(label, v, speed);
        dragCursor();
        return changed;
    }

    // A square button with a small pixel icon (play, pause, stop, lock)
    static bool iconButton(const char* id, Icon icon, bool active = false, bool enabled = true,
                           const char* tip = nullptr) {
        const float s = ImGui::GetFrameHeight();
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton(id, {s, s});
        ImGui::EndDisabled();
        const bool hovered = ImGui::IsItemHovered();
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tip);

        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        ImDrawList*  dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(col(active ? UI::accent : hovered ? UI::buttonHover : UI::button)));
        dl->AddRect(p0, p1, ImGui::GetColorU32(col(UI::border)));

        const ImU32 ink = ImGui::GetColorU32(col(active ? UI::panel : enabled ? UI::fg : UI::dim));
        const int   h   = std::max(3, int(s * 0.2f));   // half the glyph size, in pixels
        const float cx  = std::floor((p0.x + p1.x) / 2), cy = std::floor((p0.y + p1.y) / 2);
        auto box = [&](float x0, float y0, float x1, float y1) {
            dl->AddRectFilled({cx + x0, cy + y0}, {cx + x1, cy + y1}, ink);
        };

        switch (icon) {
        case Icon::Stop:
            box(-h, -h, h, h);
            break;
        case Icon::Pause: {
            const int w = std::max(2, h * 2 / 3);
            box(-h, -h, -h + w, h);
            box(h - w, -h, h, h);
            break;
        }
        case Icon::Play: {
            const int n = h + h / 2 + 1;   // a triangle made of 1-pixel columns
            for (int i = 0; i < n; ++i) {
                const float half = std::round(h * (1.0f - i / float(n)));
                box(float(-n / 2 + i), -half, float(-n / 2 + i + 1), half);
            }
            break;
        }
        case Icon::Lock:
            box(-h, 0, h, h);                    // body
            box(-h + 1, -h, -h + 2, 0);          // closed shackle
            box(h - 2, -h, h - 1, 0);
            box(-h + 1, -h, h - 1, -h + 1);
            break;
        case Icon::Unlock:
            box(-h, 0, h, h);                    // body
            box(-h + 1, -h - 2, -h + 2, 0);      // open shackle
            box(-h + 1, -h - 2, h - 1, -h - 1);
            box(h - 2, -h - 2, h - 1, -h + 1);
            break;
        }
        return clicked;
    }

    // Shorten a name with ".." so it fits in `width` pixels
    static std::string fitText(std::string s, float width) {
        if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
        while (s.size() > 1 && ImGui::CalcTextSize((s + "..").c_str()).x > width) s.pop_back();
        return s + "..";
    }

    void applyStyle() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = s.ChildRounding = s.PopupRounding = s.FrameRounding = 0;
        s.ScrollbarRounding = s.GrabRounding = s.TabRounding = 0;
        s.WindowBorderSize = s.FrameBorderSize = 1;
        s.WindowPadding = {8, 8};
        s.FramePadding  = {6, 4};
        s.ItemSpacing   = {8, 6};

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text]                 = col(UI::fg);
        c[ImGuiCol_TextDisabled]         = col(UI::dim);
        c[ImGuiCol_WindowBg]             = col(UI::panel);
        c[ImGuiCol_PopupBg]              = col(UI::panel);
        c[ImGuiCol_Border]               = col(UI::border);
        c[ImGuiCol_FrameBg]              = col(UI::bg);
        c[ImGuiCol_FrameBgHovered]       = col(UI::buttonHover);
        c[ImGuiCol_FrameBgActive]        = col(UI::buttonHover);
        c[ImGuiCol_TitleBg]              = col(UI::bg);
        c[ImGuiCol_TitleBgActive]        = col(UI::bg);
        c[ImGuiCol_MenuBarBg]            = col(UI::panel);
        c[ImGuiCol_ScrollbarBg]          = col(UI::bg);
        c[ImGuiCol_ScrollbarGrab]        = col(UI::button);
        c[ImGuiCol_ScrollbarGrabHovered] = col(UI::buttonHover);
        c[ImGuiCol_ScrollbarGrabActive]  = col(UI::accent);
        c[ImGuiCol_CheckMark]            = col(UI::accent);
        c[ImGuiCol_SliderGrab]           = col(UI::accent);
        c[ImGuiCol_SliderGrabActive]     = col(UI::accentHover);
        c[ImGuiCol_Button]               = col(UI::button);
        c[ImGuiCol_ButtonHovered]        = col(UI::buttonHover);
        c[ImGuiCol_ButtonActive]         = col(UI::accent);
        c[ImGuiCol_Header]               = col(UI::accent, 0.35f);
        c[ImGuiCol_HeaderHovered]        = col(UI::accent, 0.50f);
        c[ImGuiCol_HeaderActive]         = col(UI::accent, 0.70f);
        c[ImGuiCol_Separator]            = col(UI::border);
        c[ImGuiCol_Tab]                  = col(UI::bg);
        c[ImGuiCol_TabHovered]           = col(UI::accent, 0.60f);
        c[ImGuiCol_TabSelected]          = col(UI::button);
        c[ImGuiCol_DragDropTarget]       = col(UI::accent);
    }

    // The scene texture is viewW*s by viewH*s pixels, so it is shown 1:1 (no resampling)
    void ensureView(int s) {
        if (view && s == viewScale) return;
        if (view) SDL_DestroyTexture(view);
        view = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, viewW * s, viewH * s);
        SDL_SetTextureScaleMode(view, SDL_SCALEMODE_NEAREST);
        viewScale = s;
    }

    // ---- play mode ----
    // While playing, scripts own the objects: adding, deleting and undo are off.
    bool editing() const { return mode == Mode::Stopped; }

    void play() {
        if (mode == Mode::Paused) { mode = Mode::Playing; return; }
        if (mode != Mode::Stopped) return;
        scene.beginPlay();
        mode     = Mode::Playing;
        renaming = -1;
        scripting.start(scene, assets.folder());
    }

    void pause() {
        if (mode == Mode::Playing)     mode = Mode::Paused;
        else if (mode == Mode::Paused) mode = Mode::Playing;
    }

    void stop() {
        if (mode == Mode::Stopped) return;
        scripting.stop();
        scene.endPlay();
        mode     = Mode::Stopped;
        renaming = -1;
    }

    // ---- actions (shared by shortcuts, menu bar and right-click menus) ----
    void undo()              { if (editing()) { scene.undo(); renaming = -1; } }
    void redo()              { if (editing()) { scene.redo(); renaming = -1; } }
    void deleteSelected()    { if (editing()) { scene.removeSelected(); renaming = -1; } }
    void duplicateSelected() { if (editing()) { scene.duplicateSelected(); renaming = -1; } }

    void startRename() {
        if (!scene.hasSelection()) return;
        renaming    = scene.active;
        renameFocus = true;
    }

    void askDeleteAsset(const std::string& name) {
        askDelete        = name;
        openDeleteDialog = true;
    }

    // `sprite` is an asset name; empty = an object without a sprite. x, y = center.
    void addObject(float x, float y, const std::string& sprite = {}) {
        if (!editing()) return;
        const std::string name = sprite.empty() ? "Object" : fs::path(sprite).stem().string();
        scene.add(name, std::round(x), std::round(y), sprite);
        scene.select(int(scene.entities.size()) - 1);
    }

    void addObjectAtCenter(const std::string& sprite = {}) { addObject(viewW / 2.0f, viewH / 2.0f, sprite); }

    template <class F> void forSelected(F fn) {
        for (int i : scene.selection) fn(scene.entities[i]);
    }
    template <class F> void forOthers(F fn) {   // everything selected except the one the Inspector shows
        for (int i : scene.selection) if (i != scene.active) fn(scene.entities[i]);
    }

    // The one menu every right-click on an object uses
    void entityMenu() {
        if (ImGui::MenuItem("Rename", "F2")) startRename();
        ImGui::BeginDisabled(!editing());
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) deleteSelected();
        ImGui::EndDisabled();
    }

    void shortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        // typing in a text box or a menu/dialog is open: leave the keys alone
        if (io.WantTextInput ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) io.KeyShift ? redo() : undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) scene.selectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) startRename();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (assetsFocusedLast && !selectedAsset.empty()) askDeleteAsset(selectedAsset);
            else deleteSelected();
        }
    }

    void menuBar() {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) wantsQuit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, scene.canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, scene.canRedo())) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) scene.selectAll();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editing() && scene.hasSelection())) duplicateSelected();
            if (ImGui::MenuItem("Delete", "Del", false, editing() && scene.hasSelection())) deleteSelected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Add Object", nullptr, false, editing())) addObjectAtCenter();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("Log info"))    SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "Info test");
            if (ImGui::MenuItem("Log warning")) SDL_LogWarn (SDL_LOG_CATEGORY_APPLICATION, "Warning test");
            if (ImGui::MenuItem("Log error"))   SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error test");
            ImGui::Separator();
            if (ImGui::MenuItem("Crash: exception"))    throw std::runtime_error("Test exception");
            if (ImGui::MenuItem("Crash: null pointer")) *(volatile int*)nullptr = 1;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void hierarchy(float x, float y, float w, float h) {
        if (panel("Hierarchy", x, y, w, h)) {
            const ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < int(scene.entities.size()); ++i) {
                Entity& e = scene.entities[i];
                ImGui::PushID(i);
                if (renaming == i) {
                    if (renameFocus) { ImGui::SetKeyboardFocusHere(); renameFocus = false; }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##rename", &e.name,
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (ImGui::IsItemDeactivated()) renaming = -1;   // Enter, Esc or click away
                } else {
                    if (ImGui::Selectable(e.name.c_str(), scene.isSelected(i),
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (io.KeyShift) {
                            if (anchor < 0) anchor = i;
                            scene.selectRange(anchor, i);
                        } else if (io.KeyCtrl) {
                            scene.toggle(i);
                            anchor = i;
                        } else {
                            scene.select(i);
                            anchor = i;
                            if (ImGui::IsMouseDoubleClicked(0)) startRename();
                        }
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        if (!scene.isSelected(i)) { scene.select(i); anchor = i; }
                        else scene.active = i;
                    }
                    if (ImGui::BeginPopupContextItem(nullptr, rightClick)) {
                        entityMenu();
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }

            if (ImGui::BeginPopupContextWindow("hierarchyMenu", rightClickEmpty)) {
                if (ImGui::MenuItem("Add Object", nullptr, false, editing())) addObjectAtCenter();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    // Play / Pause / Stop, centered at the top of the Scene panel
    void toolbar() {
        const float s   = ImGui::GetFrameHeight();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetWindowWidth() - (3 * s + 2 * gap)) / 2));

        if (iconButton("play", Icon::Play, mode == Mode::Playing, true, "Play")) play();
        ImGui::SameLine();
        if (iconButton("pause", Icon::Pause, mode == Mode::Paused, mode != Mode::Stopped, "Pause")) pause();
        ImGui::SameLine();
        if (iconButton("stop", Icon::Stop, false, mode != Mode::Stopped, "Stop")) stop();
    }

    void sceneView(float x, float y, float w, float h) {
        if (panel("Scene", x, y, w, h, ImGuiWindowFlags_NoScrollbar)) {
            toolbar();

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const int    s     = std::max(1, int(std::min(avail.x / viewW, avail.y / viewH)));
            ensureView(s);

            const ImVec2 size(float(viewW * s), float(viewH * s));
            const ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos({pos.x + std::max(0.f, (avail.x - size.x) / 2),
                                 pos.y + std::max(0.f, (avail.y - size.y) / 2)});

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(tid(view), size);
            if (mode != Mode::Stopped)   // a colored frame while playing / paused
                ImGui::GetWindowDrawList()->AddRect(origin, {origin.x + size.x, origin.y + size.y},
                    ImGui::GetColorU32(col(mode == Mode::Paused ? UI::warn : UI::accent)), 0.0f, 0, 3.0f);

            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("viewport", size);
            dragInScene(origin, float(s));
            sceneDrop(origin, float(s));
            sceneMenu(origin, float(s));
        }
        ImGui::End();
    }

    // Click to select (Ctrl/Shift adds), drag a selected object to move the selection,
    // drag on empty space for a selection box. Call right after the viewport's InvisibleButton.
    void dragInScene(ImVec2 origin, float s) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2   m  = ImGui::GetMousePos();
        const float    mx = (m.x - origin.x) / s;
        const float    my = (m.y - origin.y) / s;
        const bool additive = io.KeyCtrl || io.KeyShift;

        if (ImGui::IsItemActivated()) {
            const int hit = scene.pick(mx, my);
            drag  = Drag::None;
            grabX = mx;
            grabY = my;
            if (hit >= 0) {
                if (additive)                     scene.toggle(hit);
                else if (!scene.isSelected(hit))  scene.select(hit);
                else                              scene.active = hit;

                if (scene.isSelected(hit)) {
                    drag = Drag::Move;
                    origins.clear();
                    for (int i : scene.selection) origins.push_back({i, {scene.entities[i].x, scene.entities[i].y}});
                }
            } else {
                drag = Drag::Marquee;
                marqueeBase = additive ? scene.selection : std::vector<int>{};
                if (!additive) scene.select(-1);
            }
        }
        if (!ImGui::IsItemActive()) drag = Drag::None;

        if (drag == Drag::Move) {
            const float dx = std::round(mx - grabX), dy = std::round(my - grabY);
            for (const auto& [i, p] : origins) {
                if (i >= int(scene.entities.size())) continue;
                scene.entities[i].x = p.x + dx;
                scene.entities[i].y = p.y + dy;
            }
        }

        if (drag == Drag::Marquee) {
            const float x0 = std::min(grabX, mx), x1 = std::max(grabX, mx);
            const float y0 = std::min(grabY, my), y1 = std::max(grabY, my);

            std::vector<int> sel = marqueeBase;
            for (int i : scene.inRect(x0, y0, x1, y1)) sel.push_back(i);
            scene.setSelection(sel, scene.active);

            const ImVec2 a(origin.x + x0 * s, origin.y + y0 * s), b(origin.x + x1 * s, origin.y + y1 * s);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, b, ImGui::GetColorU32(col(UI::accent, 0.15f)));
            dl->AddRect(a, b, ImGui::GetColorU32(col(UI::accent)));
        }

        if (drag == Drag::Move || (drag == Drag::None && ImGui::IsItemHovered() && scene.pick(mx, my) >= 0))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // A sprite dropped on the Scene becomes an object under the mouse
    void sceneDrop(ImVec2 origin, float s) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SPRITE")) {
            const ImVec2 m = ImGui::GetMousePos();
            addObject((m.x - origin.x) / s, (m.y - origin.y) / s, static_cast<const char*>(p->Data));
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click in the Scene: select what is under the mouse, then show the menu
    void sceneMenu(ImVec2 origin, float s) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const ImVec2 m = ImGui::GetMousePos();
            clickX = (m.x - origin.x) / s;
            clickY = (m.y - origin.y) / s;
            const int hit = scene.pick(clickX, clickY);
            if (hit < 0)                     scene.select(-1);
            else if (!scene.isSelected(hit)) scene.select(hit);
            else                             scene.active = hit;
        }
        if (ImGui::BeginPopupContextItem("sceneMenu", rightClick)) {
            if (scene.hasSelection()) entityMenu();
            else if (ImGui::MenuItem("Add Object", nullptr, false, editing())) addObject(clickX, clickY);
            ImGui::EndPopup();
        }
    }

    // Pick a sprite (or script) from a list, or drop one from the Assets panel.
    // Returns true when it changed.
    bool assetField(const char* label, std::string& value, bool scripts) {
        bool changed = false;
        if (ImGui::BeginCombo(label, value.empty() ? "None" : value.c_str())) {
            if (ImGui::Selectable("None", value.empty())) { value.clear(); changed = true; }
            for (const Asset& a : assets.all())
                if (a.script == scripts && ImGui::Selectable(a.name.c_str(), a.name == value)) {
                    value   = a.name;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(scripts ? "SCRIPT" : "SPRITE")) {
                value   = static_cast<const char*>(p->Data);
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        return changed;
    }

    void transformFields(Entity& e) {
        dragFloat2("Position", &e.x);
        dragFloat("Rotation", &e.rotation);

        // Scale, with a lock that keeps X and Y equal
        if (iconButton("scaleLock", scaleLocked ? Icon::Lock : Icon::Unlock, scaleLocked, true,
                       scaleLocked ? "X and Y change together" : "Click to link X and Y")) {
            scaleLocked = !scaleLocked;
            if (scaleLocked) forSelected([](Entity& o) { o.sy = o.sx; });
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        const float ox = e.sx;
        if (dragFloat2("Scale", &e.sx, 0.02f, 0.05f, 50.0f, "%.2f") && scaleLocked) {
            if (e.sx != ox) e.sy = e.sx;
            else            e.sx = e.sy;
        }
    }

    void rendererFields(Entity& e) {
        ImGui::Checkbox("Enabled", &e.visible);
        assetField("Sprite", e.sprite, false);

        ImGui::Checkbox("Flip X", &e.flipX);
        ImGui::SameLine();
        ImGui::Checkbox("Flip Y", &e.flipY);

        static const char* const sides[] = {"Right", "Up", "Left", "Down"};
        ImGui::Combo("Front", &e.front, sides, 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Which side of the artwork is its front.\nScripts get it as look_x / look_y.\nThe yellow arrow in the Scene shows it.");

        dragInt("Z-Index", &e.zIndex);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher numbers are drawn on top.");

        ImGui::ColorEdit4("Tint", e.tint, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);
    }

    // The script's public variables: one field each, set on every selected object that uses the same script
    void scriptVars(const Entity& e) {
        const Asset* a = assets.find(e.script);
        if (!a) {
            ImGui::TextColored(col(UI::error), "Script file not found");
            return;
        }
        const ScriptInfo& info = scripting.info(assets.folder(), e.script, a->stamp);
        if (!info.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(UI::error));
            ImGui::TextWrapped("%s", info.error.c_str());
            ImGui::PopStyleColor();
            return;
        }

        for (const Var& def : info.defaults) {
            Var cur = def;
            if (const Var* v = findVar(e.vars, def.name); v && v->type == def.type) cur = *v;

            bool changed = false;
            ImGui::PushID(def.name.c_str());
            if (def.type == 'n') {
                changed = dragFloat(def.name.c_str(), &cur.number, 0.1f, 0, 0, "%g");
            } else if (def.type == 'b') {
                bool b = cur.number != 0;
                if (ImGui::Checkbox(def.name.c_str(), &b)) { cur.number = b ? 1.0f : 0.0f; changed = true; }
            } else {
                changed = ImGui::InputText(def.name.c_str(), &cur.text);
            }
            ImGui::PopID();

            if (changed)
                forSelected([&](Entity& o) { if (o.script == e.script) setVar(o.vars, cur); });
        }
    }

    // Position moves everyone by the same amount; every other field that changed is set on everyone
    void syncOthers(const Entity& before, const Entity& e) {
        if (scene.selection.size() < 2) return;
        const bool tintChanged = !std::equal(std::begin(e.tint), std::end(e.tint), std::begin(before.tint));
        forOthers([&](Entity& o) {
            o.x += e.x - before.x;
            o.y += e.y - before.y;
            if (e.rotation != before.rotation) o.rotation = e.rotation;
            if (e.sx != before.sx)             o.sx = e.sx;
            if (e.sy != before.sy)             o.sy = e.sy;
            if (e.visible != before.visible)   o.visible = e.visible;
            if (e.sprite != before.sprite)     o.sprite = e.sprite;
            if (e.flipX != before.flipX)       o.flipX = e.flipX;
            if (e.flipY != before.flipY)       o.flipY = e.flipY;
            if (e.front != before.front)       o.front = e.front;
            if (e.zIndex != before.zIndex)     o.zIndex = e.zIndex;
            if (tintChanged)                   std::copy(std::begin(e.tint), std::end(e.tint), std::begin(o.tint));
            if (e.script != before.script)     { o.script = e.script; o.vars.clear(); }
        });
    }

    void inspector(float x, float y, float w, float h) {
        if (panel("Inspector", x, y, w, h)) {
            if (scene.hasSelection()) {
                Entity&      e = scene.entities[scene.active];
                const Entity before = e;

                if (scene.selection.size() > 1) ImGui::TextDisabled("%d objects selected", int(scene.selection.size()));
                else                            ImGui::InputText("Name", &e.name);

                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                    transformFields(e);

                if (ImGui::CollapsingHeader("Sprite Renderer", ImGuiTreeNodeFlags_DefaultOpen))
                    rendererFields(e);

                if (ImGui::CollapsingHeader("Script##section", ImGuiTreeNodeFlags_DefaultOpen)) {
                    assetField("Script##file", e.script, true);
                    if (e.script != before.script) e.vars.clear();   // the old values belong to the old script
                    if (!e.script.empty()) scriptVars(e);
                }

                syncOthers(before, e);

                ImGui::Separator();
                ImGui::BeginDisabled(!editing());
                if (ImGui::Button("Delete")) deleteSelected();
                ImGui::EndDisabled();
            } else {
                ImGui::TextDisabled("Nothing selected");
            }

            // Right-click on empty space (the resets come first: entityMenu may delete objects)
            if (scene.hasSelection() && ImGui::BeginPopupContextWindow("inspectorMenu", rightClickEmpty)) {
                if (ImGui::MenuItem("Reset Position")) forSelected([](Entity& o) { o.x = o.y = 0; });
                if (ImGui::MenuItem("Reset Rotation")) forSelected([](Entity& o) { o.rotation = 0; });
                if (ImGui::MenuItem("Reset Scale"))    forSelected([](Entity& o) { o.sx = o.sy = 1; });
                ImGui::Separator();
                entityMenu();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    void bottomPanel(float x, float y, float w, float h) {
        if (panel("Bottom", x, y, w, h, ImGuiWindowFlags_NoTitleBar) && ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Console")) {
                console();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Assets")) {
                assetsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void console() {
        const SDL_Color colors[3] = {UI::fg, UI::warn, UI::error};
        const auto& log = Log::entries();

        ImGui::BeginChild("log");
        for (const LogEntry& e : log) {
            const int k = e.level >= SDL_LOG_PRIORITY_ERROR ? 2 : e.level == SDL_LOG_PRIORITY_WARN ? 1 : 0;
            ImGui::Image(tid(logIcons[k]), {16, 16});
            ImGui::SameLine();
            ImGui::TextColored(col(colors[k]), "%s", e.text.c_str());
        }
        if (log.size() != lastLogCount) {
            ImGui::SetScrollHereY(1.0f);
            lastLogCount = log.size();
        }
        ImGui::EndChild();
    }

    void assetsTab() {
        assetsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (ImGui::Button("Refresh")) refreshAssets = true;
        ImGui::SameLine();
        if (ImGui::Button("New Script")) {
            selectedAsset = assets.createScript();
            refreshAssets = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", assets.folder().c_str());

        ImGui::BeginChild("assetGrid");
        const auto& list = assets.all();
        if (list.empty())
            ImGui::TextDisabled("Nothing here yet. Drop PNG or Lua files onto this window, or use New Script.");

        const float thumb   = 64;
        const float gap     = ImGui::GetStyle().ItemSpacing.x;
        const int   columns = std::max(1, int((ImGui::GetContentRegionAvail().x + gap) / (thumb + gap)));

        for (int i = 0; i < int(list.size()); ++i) {
            ImGui::PushID(i);
            assetCell(list[i], thumb);
            ImGui::PopID();
            if ((i + 1) % columns != 0 && i + 1 < int(list.size())) ImGui::SameLine();
        }
        ImGui::EndChild();
    }

    // One tile: click to select, double-click to open, drag to use it, right-click for options
    void assetCell(const Asset& a, float thumb) {
        float tw = 16, th = 16;
        if (a.texture) SDL_GetTextureSize(a.texture, &tw, &th);
        float sc = std::min(thumb / tw, thumb / th);
        if (sc >= 1) sc = std::floor(sc);   // whole-number zoom keeps pixel art crisp
        const ImVec2 size(tw * sc, th * sc);

        ImGui::BeginGroup();
        if (ImGui::InvisibleButton("cell", {thumb, thumb})) selectedAsset = a.name;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) selectedAsset = a.name;
        const bool   hovered = ImGui::IsItemHovered();
        const bool   chosen  = a.name == selectedAsset;
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(col(chosen ? UI::button : UI::bg)));
        if (a.texture) {
            const ImVec2 q0(std::floor(p0.x + (thumb - size.x) / 2), std::floor(p0.y + (thumb - size.y) / 2));
            dl->AddImage(tid(a.texture), q0, {q0.x + size.x, q0.y + size.y});
        } else {   // scripts have no picture: show a tag instead
            const ImVec2 ts = ImGui::CalcTextSize("LUA");
            dl->AddText({std::floor(p0.x + (thumb - ts.x) / 2), std::floor(p0.y + (thumb - ts.y) / 2)},
                        ImGui::GetColorU32(col(UI::accent)), "LUA");
        }
        dl->AddRect(p0, p1, ImGui::GetColorU32(col(chosen || hovered ? UI::accent : UI::border)));

        if (hovered && ImGui::IsMouseDoubleClicked(0)) assets.open(a.name);

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(a.script ? "SCRIPT" : "SPRITE", a.name.c_str(), a.name.size() + 1);
            if (a.texture) ImGui::Image(tid(a.texture), {size.x * 0.5f, size.y * 0.5f});
            ImGui::TextUnformatted(a.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem("assetMenu", rightClick)) {
            if (ImGui::MenuItem("Open")) assets.open(a.name);
            if (!a.script) {
                if (ImGui::MenuItem("Add to Scene", nullptr, false, editing())) addObjectAtCenter(a.name);
                ImGui::Separator();
                if (ImGui::MenuItem("Pixel filter (crisp)", nullptr, !a.smooth)) assets.setSmooth(a.name, false);
                if (ImGui::MenuItem("Smooth filter",        nullptr,  a.smooth)) assets.setSmooth(a.name, true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete...", "Del")) askDeleteAsset(a.name);
            ImGui::EndPopup();
        }

        if (hovered && !ImGui::GetDragDropPayload()) {
            if (a.script) ImGui::SetTooltip("%s\nLua script", a.name.c_str());
            else ImGui::SetTooltip("%s\n%d x %d  (%s)", a.name.c_str(), int(tw), int(th), a.smooth ? "Smooth" : "Pixel");
        }

        ImGui::TextUnformatted(fitText(fs::path(a.name).filename().string(), thumb).c_str());
        ImGui::EndGroup();
    }

    // Asks before an asset file is moved to the Recycle Bin
    void deleteAssetDialog() {
        if (openDeleteDialog) {
            ImGui::OpenPopup("Delete asset");
            openDeleteDialog = false;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
        if (ImGui::BeginPopupModal("Delete asset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("Delete \"%s\"?", askDelete.c_str());
            ImGui::TextDisabled("The file moves to the Recycle Bin.");
            ImGui::TextDisabled("Objects that use it will no longer find it.");
            ImGui::Spacing();
            if (ImGui::Button("Delete", {120, 0})) {
                deleteAsset = askDelete;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    Window&       win;
    SDL_Renderer* r;
    AssetStore    assets;
    SDL_Texture*  view        = nullptr;
    int           viewScale   = 0;
    SDL_Texture*  logIcons[3] = {};
    Scene         scene;
    Scripting     scripting;
    Mode          mode          = Mode::Stopped;
    Uint64        lastTicks     = 0;
    size_t        lastLogCount  = 0;
    bool          refreshAssets = false;

    // Hierarchy
    int   renaming    = -1;      // row being renamed
    bool  renameFocus = false;
    int   anchor      = -1;      // where Shift+click ranges start

    // Scene view
    Drag  drag  = Drag::None;
    float grabX = 0, grabY = 0;                      // mouse position (scene units) when the drag began
    std::vector<std::pair<int, SDL_FPoint>> origins; // where the moved objects started
    std::vector<int> marqueeBase;                    // selection kept while adding with a box
    float clickX = 0, clickY = 0;                    // where the last right-click landed

    // Inspector
    bool scaleLocked = false;

    // Assets
    std::string selectedAsset;
    std::string askDelete, deleteAsset;
    bool        openDeleteDialog = false;
    bool        assetsFocused = false, assetsFocusedLast = false;
};