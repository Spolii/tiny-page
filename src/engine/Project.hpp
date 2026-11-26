#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include "LuaFile.hpp"

namespace fs = std::filesystem;

// A project is a folder:  project.lua, assets/, scenes/
struct Project {
    fs::path    root;
    std::string name;
    std::string scene;   // the open scene (no extension); empty = untitled

    fs::path assetsDir() const { return root / "assets"; }
    fs::path scenesDir() const { return root / "scenes"; }
    fs::path scenePath(const std::string& sceneName) const { return scenesDir() / (sceneName + ".scene"); }

    static bool exists(const fs::path& folder) {
        std::error_code ec;
        return fs::exists(folder / "project.lua", ec);
    }

    static std::string nameOf(const fs::path& folder) {
        std::string n = folder.filename().string();
        if (n.empty()) n = folder.parent_path().filename().string();   // "D:\Games\MyGame\"
        return n.empty() ? "Project" : n;
    }

    // Make the folders and project.lua of a new project (its name is the folder's name)
    static bool create(const fs::path& folder) {
        std::error_code ec;
        fs::create_directories(folder / "assets", ec);
        fs::create_directories(folder / "scenes", ec);
        if (ec) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create the project: %s", ec.message().c_str());
            return false;
        }
        Project p;
        p.root = folder;
        p.name = nameOf(folder);
        p.saveInfo();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Created project %s", p.name.c_str());
        return true;
    }

    bool open(const fs::path& folder) {
        LuaFile file(folder / "project.lua");
        if (!file.ok()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read project.lua: %s", file.error().c_str());
            return false;
        }
        root  = folder;
        name  = lua_data::str(file.state(), "name", nameOf(folder));
        scene = lua_data::str(file.state(), "main_scene");
        return true;
    }

    void saveInfo() const {
        std::string out = "-- tiny-page project\nreturn {\n  name = " + lua_data::quote(name) + ",\n";
        if (!scene.empty()) out += "  main_scene = " + lua_data::quote(scene) + ",\n";
        out += "}\n";
        SDL_SaveFile((root / "project.lua").string().c_str(), out.data(), out.size());
    }

    // Names of the saved scenes, sorted
    std::vector<std::string> scenes() const {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& f : fs::directory_iterator(scenesDir(), ec))
            if (f.is_regular_file(ec) && f.path().extension() == ".scene") names.push_back(f.path().stem().string());
        std::sort(names.begin(), names.end());
        return names;
    }
};