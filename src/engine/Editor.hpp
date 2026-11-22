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
#include <stdexcept>
#include "AssetStore.hpp"
#include "Scene.hpp"
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
            {"Icon", "icon.png", 40, 40}, {"Info", "info.png", 120, 60},
            {"Warning", "warning.png", 200, 90}, {"Error", "error.png", 120, 130},
        };
        for (const auto& d : demo)
            if (assets.find(d.sprite)) scene.add(d.name, d.x, d.y, d.sprite);
        scene.resetHistory();
    }

    ~Editor() {
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
        // Safe moment to reload textures: nothing from this frame references them yet
        if (refreshAssets) {
            assets.refresh();
            refreshAssets = false;
        }

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

        // One finished drag / rename / action = one undo step
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

    // Show a "drag sideways" cursor on the last widget
    static void dragCursor() {
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    static void dragFloat(const char* label, float* v, float speed = 1) {
        ImGui::DragFloat(label, v, speed, 0, 0, "%.1f");
        dragCursor();
    }
    static void dragFloat2(const char* label, float* v, float speed = 1) {
        ImGui::DragFloat2(label, v, speed, 0, 0, "%.1f");
        dragCursor();
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

    // ---- actions (shared by shortcuts, menu bar and right-click menus) ----
    void undo()              { scene.undo(); renaming = -1; }
    void redo()              { scene.redo(); renaming = -1; }
    void deleteSelected()    { scene.removeSelected(); renaming = -1; }
    void duplicateSelected() { scene.duplicateSelected(); renaming = -1; }
    void bringToFront()      { scene.bringToFront(); renaming = -1; }
    void sendToBack()        { scene.sendToBack(); renaming = -1; }

    void startRename() {
        if (!scene.hasSelection()) return;
        renaming    = scene.selected;
        renameFocus = true;
    }

    // `sprite` is an asset name; empty = an object without a sprite
    void addObject(float x, float y, const std::string& sprite = {}) {
        const std::string name = sprite.empty() ? "Object" : fs::path(sprite).stem().string();
        scene.add(name, std::round(x), std::round(y), sprite);
        scene.selected = int(scene.entities.size()) - 1;
    }

    void addObjectAtCenter(const std::string& sprite = {}) {
        const SDL_FPoint s = assets.size(sprite);
        addObject(viewW / 2.0f - s.x / 2, viewH / 2.0f - s.y / 2, sprite);
    }

    // The one menu every right-click on an object uses
    void entityMenu() {
        if (ImGui::MenuItem("Rename", "F2"))        startRename();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Bring to Front"))      bringToFront();
        if (ImGui::MenuItem("Send to Back"))        sendToBack();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del"))       deleteSelected();
    }

    void shortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;   // typing in a text box: leave the keys alone

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) io.KeyShift ? redo() : undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) startRename();
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
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, scene.hasSelection())) duplicateSelected();
            if (ImGui::MenuItem("Delete", "Del", false, scene.hasSelection())) deleteSelected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Add Object")) addObjectAtCenter();
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
                    if (ImGui::Selectable(e.name.c_str(), scene.selected == i,
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                        scene.selected = i;
                        if (ImGui::IsMouseDoubleClicked(0)) startRename();
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) scene.selected = i;
                    if (ImGui::BeginPopupContextItem(nullptr, rightClick)) {
                        entityMenu();
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }

            if (ImGui::BeginPopupContextWindow("hierarchyMenu", rightClickEmpty)) {
                if (ImGui::MenuItem("Add Object")) addObjectAtCenter();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    void sceneView(float x, float y, float w, float h) {
        if (panel("Scene", x, y, w, h, ImGuiWindowFlags_NoScrollbar)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const int    s     = std::max(1, int(std::min(avail.x / viewW, avail.y / viewH)));
            ensureView(s);

            const ImVec2 size(float(viewW * s), float(viewH * s));
            const ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPos({pos.x + std::max(0.f, (avail.x - size.x) / 2),
                                 pos.y + std::max(0.f, (avail.y - size.y) / 2)});

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(tid(view), size);
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("viewport", size);
            dragInScene(origin, float(s));
            sceneDrop(origin, float(s));
            sceneMenu(origin, float(s));
        }
        ImGui::End();
    }

    // Click to select, drag to move (call right after the viewport's InvisibleButton)
    void dragInScene(ImVec2 origin, float s) {
        const ImVec2 m  = ImGui::GetMousePos();
        const float  mx = (m.x - origin.x) / s;
        const float  my = (m.y - origin.y) / s;

        if (ImGui::IsItemActivated()) {
            scene.selected = scene.pick(mx, my);
            dragging       = scene.hasSelection();
            if (dragging) {
                grabX = mx - scene.entities[scene.selected].x;
                grabY = my - scene.entities[scene.selected].y;
            }
        }
        if (!ImGui::IsItemActive()) dragging = false;

        if (dragging) {
            Entity& e = scene.entities[scene.selected];
            e.x = std::round(mx - grabX);
            e.y = std::round(my - grabY);
        }
        if (dragging || (ImGui::IsItemHovered() && scene.pick(mx, my) >= 0))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // An asset dropped on the Scene becomes an object under the mouse
    void sceneDrop(ImVec2 origin, float s) {
        if (!ImGui::BeginDragDropTarget()) return;
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET")) {
            const std::string  name = static_cast<const char*>(p->Data);
            const SDL_FPoint   size = assets.size(name);
            const ImVec2       m    = ImGui::GetMousePos();
            addObject((m.x - origin.x) / s - size.x / 2, (m.y - origin.y) / s - size.y / 2, name);
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click in the Scene: select what is under the mouse, then show the menu
    void sceneMenu(ImVec2 origin, float s) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const ImVec2 m = ImGui::GetMousePos();
            clickX = (m.x - origin.x) / s;
            clickY = (m.y - origin.y) / s;
            scene.selected = scene.pick(clickX, clickY);
        }
        if (ImGui::BeginPopupContextItem("sceneMenu", rightClick)) {
            if (scene.hasSelection()) entityMenu();
            else if (ImGui::MenuItem("Add Object")) addObject(clickX - 8, clickY - 8);
            ImGui::EndPopup();
        }
    }

    // Pick an asset from a list, or drop one from the Assets panel
    void spriteField(Entity& e) {
        if (ImGui::BeginCombo("Sprite", e.sprite.empty() ? "None" : e.sprite.c_str())) {
            if (ImGui::Selectable("None", e.sprite.empty())) e.sprite.clear();
            for (const Asset& a : assets.all())
                if (ImGui::Selectable(a.name.c_str(), a.name == e.sprite)) e.sprite = a.name;
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET"))
                e.sprite = static_cast<const char*>(p->Data);
            ImGui::EndDragDropTarget();
        }
    }

    void inspector(float x, float y, float w, float h) {
        if (panel("Inspector", x, y, w, h)) {
            if (scene.hasSelection()) {
                Entity& e = scene.entities[scene.selected];
                ImGui::InputText("Name", &e.name);
                dragFloat2("Position", &e.x);
                dragFloat("Rotation", &e.rotation);
                spriteField(e);
                ImGui::Separator();
                if (ImGui::Button("Delete")) deleteSelected();
            } else {
                ImGui::TextDisabled("Nothing selected");
            }

            // Right-click on empty space (the resets come first: entityMenu may delete the object)
            if (scene.hasSelection() && ImGui::BeginPopupContextWindow("inspectorMenu", rightClickEmpty)) {
                Entity& e = scene.entities[scene.selected];
                if (ImGui::MenuItem("Reset Position")) e.x = e.y = 0;
                if (ImGui::MenuItem("Reset Rotation")) e.rotation = 0;
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
        if (ImGui::Button("Refresh")) refreshAssets = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", assets.folder().c_str());

        ImGui::BeginChild("assetGrid");
        const auto& list = assets.all();
        if (list.empty())
            ImGui::TextDisabled("No PNG files yet. Drop PNGs onto this window or copy them into the folder above.");

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

    // One thumbnail: drag it into the Scene, right-click for options
    void assetCell(const Asset& a, float thumb) {
        float tw = 16, th = 16;
        SDL_GetTextureSize(a.texture, &tw, &th);
        float sc = std::min(thumb / tw, thumb / th);
        if (sc >= 1) sc = std::floor(sc);   // whole-number zoom keeps pixel art crisp
        const ImVec2 size(tw * sc, th * sc);

        ImGui::BeginGroup();
        ImGui::InvisibleButton("cell", {thumb, thumb});
        const bool   hovered = ImGui::IsItemHovered();
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        const ImVec2 q0(std::floor(p0.x + (thumb - size.x) / 2), std::floor(p0.y + (thumb - size.y) / 2));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(col(UI::bg)));
        dl->AddImage(tid(a.texture), q0, {q0.x + size.x, q0.y + size.y});
        dl->AddRect(p0, p1, ImGui::GetColorU32(col(hovered ? UI::accent : UI::border)));

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ASSET", a.name.c_str(), a.name.size() + 1);
            ImGui::Image(tid(a.texture), {size.x * 0.5f, size.y * 0.5f});
            ImGui::TextUnformatted(a.name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem("assetMenu", rightClick)) {
            if (ImGui::MenuItem("Add to Scene")) addObjectAtCenter(a.name);
            ImGui::Separator();
            if (ImGui::MenuItem("Pixel filter (crisp)", nullptr, !a.smooth)) assets.setSmooth(a.name, false);
            if (ImGui::MenuItem("Smooth filter",        nullptr,  a.smooth)) assets.setSmooth(a.name, true);
            ImGui::EndPopup();
        }

        if (hovered && !ImGui::GetDragDropPayload())
            ImGui::SetTooltip("%s\n%d x %d  (%s)", a.name.c_str(), int(tw), int(th), a.smooth ? "Smooth" : "Pixel");

        ImGui::TextUnformatted(fitText(fs::path(a.name).filename().string(), thumb).c_str());
        ImGui::EndGroup();
    }

    Window&       win;
    SDL_Renderer* r;
    AssetStore    assets;
    SDL_Texture*  view        = nullptr;
    int           viewScale   = 0;
    SDL_Texture*  logIcons[3] = {};
    Scene         scene;
    size_t        lastLogCount = 0;
    bool          refreshAssets = false;

    int   renaming    = -1;      // Hierarchy row being renamed
    bool  renameFocus = false;
    bool  dragging    = false;   // dragging a sprite in the Scene view
    float grabX = 0, grabY = 0;
    float clickX = 0, clickY = 0;   // where the last right-click landed, in scene units
};