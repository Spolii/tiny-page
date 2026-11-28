#pragma once
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>
#include "LuaFile.hpp"
#include "Scene.hpp"

// Scenes are saved as Lua tables (.scene files): readable, and easy to compare in git.
// Only values that differ from the defaults are written.
namespace SceneFile {

inline const char* const fronts[4] = {"right", "up", "left", "down"};

// { 1, 0.5, 0 }
inline std::string floats(const float* v, int count) {
    std::string out = "{ ";
    for (int k = 0; k < count; ++k) out += lua_data::number(v[k]) + (k + 1 < count ? ", " : " ");
    return out + "}";
}

// Reads a list of numbers stored under `key` in the table on top of the Lua stack
inline void readFloats(lua_State* L, const char* key, float* dst, int count) {
    lua_getfield(L, -1, key);
    if (lua_istable(L, -1))
        for (int k = 0; k < count; ++k) {
            lua_rawgeti(L, -1, k + 1);
            if (lua_isnumber(L, -1)) dst[k] = float(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
    lua_pop(L, 1);
}

inline bool save(const std::filesystem::path& path, const std::vector<Entity>& entities) {
    using namespace lua_data;
    const Entity base;   // the defaults, to leave out values nobody changed
    std::string  out = "-- tiny-page scene\nreturn {\n  entities = {\n";
    for (const Entity& e : entities) {
        out += "    {\n";
        out += "      name = " + quote(e.name) + ",\n";
        out += "      x = " + number(e.x) + ", y = " + number(e.y) + ",\n";
        if (e.rotation != 0)        out += "      rotation = " + number(e.rotation) + ",\n";
        if (e.sx != 1 || e.sy != 1) out += "      scale_x = " + number(e.sx) + ", scale_y = " + number(e.sy) + ",\n";
        if (!e.visible)             out += "      visible = false,\n";
        if (!e.sprite.empty())      out += "      sprite = " + quote(e.sprite) + ",\n";
        if (e.flipX)                out += "      flip_x = true,\n";
        if (e.flipY)                out += "      flip_y = true,\n";
        if (e.front != 0)           out += std::string("      front = \"") + fronts[std::clamp(e.front, 0, 3)] + "\",\n";
        if (e.zIndex != 0)          out += "      z_index = " + std::to_string(e.zIndex) + ",\n";
        if (!std::equal(std::begin(e.tint), std::end(e.tint), std::begin(base.tint)))
            out += "      tint = " + floats(e.tint, 4) + ",\n";
        if (e.camera) {
            out += "      camera = true,\n";
            if (e.zoom != 1) out += "      zoom = " + number(e.zoom) + ",\n";
            if (!std::equal(std::begin(e.background), std::end(e.background), std::begin(base.background)))
                out += "      background = " + floats(e.background, 3) + ",\n";
        }
        if (!e.script.empty())      out += "      script = " + quote(e.script) + ",\n";
        if (!e.vars.empty()) {
            out += "      vars = { ";
            for (const Var& v : e.vars) {
                out += "[" + quote(v.name) + "] = ";
                if (v.type == 'n')      out += number(v.number);
                else if (v.type == 'b') out += v.number != 0 ? "true" : "false";
                else                    out += quote(v.text);
                out += ", ";
            }
            out += "},\n";
        }
        out += "    },\n";
    }
    out += "  },\n}\n";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return SDL_SaveFile(path.string().c_str(), out.data(), out.size());
}

// Reads the entity table that is on top of the Lua stack
inline Entity readEntity(lua_State* L) {
    using namespace lua_data;
    Entity e;
    e.name     = str(L, "name", "Object");
    e.x        = num(L, "x", 0);
    e.y        = num(L, "y", 0);
    e.rotation = num(L, "rotation", 0);
    e.sx       = num(L, "scale_x", 1);
    e.sy       = num(L, "scale_y", 1);
    e.visible  = flag(L, "visible", true);
    e.sprite   = str(L, "sprite");
    e.flipX    = flag(L, "flip_x", false);
    e.flipY    = flag(L, "flip_y", false);
    e.zIndex   = int(num(L, "z_index", 0));
    e.camera   = flag(L, "camera", false);
    e.zoom     = num(L, "zoom", 1);
    e.script   = str(L, "script");
    readFloats(L, "tint", e.tint, 4);
    readFloats(L, "background", e.background, 3);

    const std::string front = str(L, "front", "right");
    e.front = 0;
    for (int i = 0; i < 4; ++i) if (front == fronts[i]) e.front = i;

    lua_getfield(L, -1, "vars");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                Var v;
                v.name = lua_tostring(L, -2);
                bool known = true;
                switch (lua_type(L, -1)) {
                case LUA_TNUMBER:  v.type = 'n'; v.number = float(lua_tonumber(L, -1)); break;
                case LUA_TBOOLEAN: v.type = 'b'; v.number = lua_toboolean(L, -1) ? 1.0f : 0.0f; break;
                case LUA_TSTRING:  v.type = 's'; v.text = lua_tostring(L, -1); break;
                default:           known = false;
                }
                if (known) e.vars.push_back(std::move(v));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    std::sort(e.vars.begin(), e.vars.end(), [](const Var& a, const Var& b) { return a.name < b.name; });
    return e;
}

// Returns an empty string on success, otherwise what went wrong
inline std::string load(const std::filesystem::path& path, std::vector<Entity>& out) {
    LuaFile file(path);
    if (!file.ok()) return file.error();

    lua_State* L = file.state();
    lua_getfield(L, -1, "entities");
    if (!lua_istable(L, -1)) return "the scene has no 'entities' list";

    out.clear();
    const int count = int(lua_rawlen(L, -1));
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(L, -1, i);
        if (lua_istable(L, -1)) out.push_back(readEntity(L));
        lua_pop(L, 1);
    }
    return {};
}

}  // namespace SceneFile