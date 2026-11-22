#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include "Scene.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// Runs the Lua script attached to each object while the editor is playing.
// A script is a .lua file with two optional functions:
//   function start(self)        -- once, when Play starts
//   function update(self, dt)   -- every frame, dt = seconds since the last frame
// `self` has x, y, rotation, scale_x, scale_y and name. Changes are copied back to the object.
// Every object gets its own copy of its script, so state in `self` or in globals is per object.
class Scripting {
public:
    ~Scripting() { stop(); }

    void start(Scene& scene, const std::string& folder) {
        stop();
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_register(L, "log", logFn);
        lua_register(L, "key_down", keyDownFn);

        instances.assign(scene.entities.size(), Instance{});
        for (size_t i = 0; i < instances.size(); ++i) {
            const std::string& script = scene.entities[i].script;
            if (!script.empty()) load(instances[i], folder + "/" + script, script);
        }
        for (size_t i = 0; i < instances.size(); ++i)
            call(instances[i], scene.entities[i], "start", -1);
    }

    void update(Scene& scene, float dt) {
        if (!L) return;
        for (size_t i = 0; i < instances.size() && i < scene.entities.size(); ++i)
            call(instances[i], scene.entities[i], "update", dt);
    }

    void stop() {
        if (L) lua_close(L);
        L = nullptr;
        instances.clear();
    }

private:
    struct Instance {
        int  env    = LUA_NOREF;   // the script's own global table
        int  self   = LUA_NOREF;   // the `self` table passed to start/update
        bool failed = false;       // after an error the script stops running
    };

    // ---- functions scripts can call ----
    static int logFn(lua_State* L) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[script] %s", luaL_tolstring(L, 1, nullptr));
        return 0;
    }

    static int keyDownFn(lua_State* L) {
        const SDL_Scancode key = SDL_GetScancodeFromName(luaL_checkstring(L, 1));
        const bool* keys = SDL_GetKeyboardState(nullptr);
        lua_pushboolean(L, key != SDL_SCANCODE_UNKNOWN && keys[key]);
        return 1;
    }

    // Pops the error message that is on top of the stack and logs it
    void report(const std::string& label) {
        const char* msg = lua_tostring(L, -1);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Script %s: %s", label.c_str(), msg ? msg : "unknown error");
        lua_pop(L, 1);
    }

    void load(Instance& in, const std::string& path, const std::string& label) {
        if (luaL_loadfile(L, path.c_str()) != LUA_OK) { report(label); return; }

        lua_newtable(L);                      // the script's environment ...
        lua_newtable(L);
        lua_pushglobaltable(L);
        lua_setfield(L, -2, "__index");       // ... which falls back to the normal globals
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        in.env = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_setupvalue(L, -2, 1);             // the file runs inside that environment

        if (lua_pcall(L, 0, 0, 0) != LUA_OK) { report(label); in.failed = true; return; }

        lua_newtable(L);
        in.self = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    void setNumber(const char* key, float v) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, key);
    }

    float getNumber(const char* key, float fallback) {
        lua_getfield(L, -1, key);
        const float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : fallback;
        lua_pop(L, 1);
        return v;
    }

    // Calls fn(self) or, when dt >= 0, fn(self, dt). `self` mirrors the object around the call.
    void call(Instance& in, Entity& e, const char* fn, float dt) {
        if (in.failed || in.env == LUA_NOREF || in.self == LUA_NOREF) return;

        lua_rawgeti(L, LUA_REGISTRYINDEX, in.env);    // [env]
        lua_rawgeti(L, LUA_REGISTRYINDEX, in.self);   // [env, self]
        setNumber("x", e.x);
        setNumber("y", e.y);
        setNumber("rotation", e.rotation);
        setNumber("scale_x", e.sx);
        setNumber("scale_y", e.sy);
        lua_pushstring(L, e.name.c_str());
        lua_setfield(L, -2, "name");

        lua_getfield(L, -2, fn);                      // [env, self, function]
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, -2);                     // argument: self
            int args = 1;
            if (dt >= 0) { lua_pushnumber(L, dt); args = 2; }
            if (lua_pcall(L, args, 0, 0) != LUA_OK) { report(e.script); in.failed = true; }

            e.x        = getNumber("x", e.x);
            e.y        = getNumber("y", e.y);
            e.rotation = getNumber("rotation", e.rotation);
            e.sx       = getNumber("scale_x", e.sx);
            e.sy       = getNumber("scale_y", e.sy);
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 2);                                // env, self
    }

    lua_State*            L = nullptr;
    std::vector<Instance> instances;   // one per object, same order as the scene
};