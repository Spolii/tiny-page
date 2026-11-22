#pragma once
#include <SDL3/SDL.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core/Assets.hpp"

#ifndef PROJECT_DIR
#define PROJECT_DIR "project"
#endif

namespace fs = std::filesystem;

// A file in project/assets: a PNG sprite (has a texture) or a Lua script (no texture)
struct Asset {
    std::string         name;   // path inside project/assets, e.g. "icon.png"
    SDL_Texture*        texture = nullptr;
    bool                script  = false;
    bool                smooth  = false;
    fs::file_time_type  stamp{};
};

// Entities refer to assets by name.
// Call refresh() only outside the UI build (it can replace textures).
class AssetStore {
public:
    explicit AssetStore(SDL_Renderer* r) : renderer(r) {
        std::error_code ec;
        fs::create_directories(dir(), ec);
        loadFilters();
        refresh();
    }

    const std::vector<Asset>& all() const { return assets; }
    std::string folder() const { return dir().string(); }

    const Asset* find(const std::string& name) const {
        for (const Asset& a : assets) if (a.name == name) return &a;
        return nullptr;
    }

    SDL_Texture* texture(const std::string& name) const {
        const Asset* a = find(name);
        return a ? a->texture : nullptr;
    }

    // Size in pixels; 16x16 when there is no sprite
    SDL_FPoint size(const std::string& name) const {
        float w = 16, h = 16;
        if (SDL_Texture* t = texture(name)) SDL_GetTextureSize(t, &w, &h);
        return {w, h};
    }

    void setSmooth(const std::string& name, bool smooth) {
        if (smooth) smoothNames.insert(name); else smoothNames.erase(name);
        for (Asset& a : assets) if (a.name == name) applyFilter(a);
        saveFilters();
    }

    // Copy a PNG or Lua file into the project; call refresh() afterwards
    bool importFile(const std::string& path) {
        const fs::path src(path);
        if (!hasExt(src, ".png") && !hasExt(src, ".lua")) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Import skipped, only PNG and Lua files are supported: %s", path.c_str());
            return false;
        }
        std::error_code ec;
        fs::copy_file(src, dir() / src.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Import failed: %s", ec.message().c_str());
            return false;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Imported %s", src.filename().string().c_str());
        return true;
    }

    // Create a new script from a template and return its name; call refresh() afterwards
    std::string createScript() {
        std::error_code ec;
        for (int n = 0; n < 1000; ++n) {
            const std::string name = n == 0 ? "NewScript.lua" : "NewScript" + std::to_string(n) + ".lua";
            if (fs::exists(dir() / name, ec)) continue;
            SDL_SaveFile((dir() / name).string().c_str(), scriptTemplate, std::strlen(scriptTemplate));
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Created %s", name.c_str());
            return name;
        }
        return {};
    }

    // Open a file with the app Windows has chosen for it
    void open(const std::string& name) const {
        const std::string path = (dir() / name).make_preferred().string();
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open %s", name.c_str());
    }

    // Move the file to the Recycle Bin; call refresh() afterwards
    bool remove(const std::string& name) {
        std::string path = (dir() / name).make_preferred().string();
        path.push_back('\0');   // the API wants a list that ends with two nulls

        SHFILEOPSTRUCTA op{};
        op.wFunc  = FO_DELETE;
        op.pFrom  = path.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        if (SHFileOperationA(&op) != 0 || op.fAnyOperationsAborted) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not delete %s", name.c_str());
            return false;
        }
        smoothNames.erase(name);
        saveFilters();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Moved %s to the Recycle Bin", name.c_str());
        return true;
    }

    // Add new files, reload changed ones, drop deleted ones
    void refresh() {
        std::error_code ec;
        std::vector<std::pair<std::string, fs::file_time_type>> found;
        for (const auto& f : fs::recursive_directory_iterator(dir(), ec)) {
            if (!f.is_regular_file(ec) || !(hasExt(f.path(), ".png") || hasExt(f.path(), ".lua"))) continue;
            found.emplace_back(fs::relative(f.path(), dir(), ec).generic_string(), f.last_write_time(ec));
        }

        for (auto it = assets.begin(); it != assets.end();) {
            const bool exists = std::any_of(found.begin(), found.end(),
                                            [&](const auto& f) { return f.first == it->name; });
            if (exists) { ++it; continue; }
            if (it->texture) SDL_DestroyTexture(it->texture);
            it = assets.erase(it);
        }

        for (const auto& [name, stamp] : found) {
            Asset* a = nullptr;
            for (Asset& x : assets) if (x.name == name) a = &x;
            if (a && a->stamp == stamp) continue;

            const bool   isScript = hasExt(name, ".lua");
            SDL_Texture* tex      = nullptr;
            if (!isScript) {
                tex = loadTextureFile(renderer, (dir() / name).string());
                if (!tex) continue;
            }

            if (a) {
                if (a->texture) SDL_DestroyTexture(a->texture);
            } else {
                assets.push_back({});
                a = &assets.back();
                a->name   = name;
                a->script = isScript;
            }
            a->texture = tex;
            a->stamp   = stamp;
            if (tex) applyFilter(*a);
        }

        std::sort(assets.begin(), assets.end(), [](const Asset& a, const Asset& b) { return a.name < b.name; });
    }

private:
    static constexpr const char* scriptTemplate =
        "-- self has: x, y, rotation, scale_x, scale_y, name\n"
        "-- helpers: log(\"text\"), key_down(\"Left\")\n"
        "\n"
        "-- Runs once when Play starts\n"
        "function start(self)\n"
        "end\n"
        "\n"
        "-- Runs every frame while playing. dt = seconds since the last frame\n"
        "function update(self, dt)\n"
        "    self.rotation = self.rotation + 90 * dt\n"
        "end\n";

    static fs::path dir()         { return fs::path(PROJECT_DIR) / "assets"; }
    static fs::path filtersFile() { return fs::path(PROJECT_DIR) / "filters.txt"; }

    static bool hasExt(const fs::path& p, const char* ext) {
        std::string e = p.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return e == ext;
    }

    void applyFilter(Asset& a) {
        a.smooth = smoothNames.contains(a.name);
        SDL_SetTextureScaleMode(a.texture, a.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }

    // filters.txt: one asset name per line = uses the Smooth filter
    void loadFilters() {
        size_t size = 0;
        char* data = static_cast<char*>(SDL_LoadFile(filtersFile().string().c_str(), &size));
        if (!data) return;
        std::istringstream in(std::string(data, size));
        SDL_free(data);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) smoothNames.insert(line);
        }
    }

    void saveFilters() const {
        std::string out;
        for (const std::string& n : smoothNames) out += n + "\n";
        SDL_SaveFile(filtersFile().string().c_str(), out.data(), out.size());
    }

    SDL_Renderer*         renderer;
    std::vector<Asset>    assets;
    std::set<std::string> smoothNames;
};