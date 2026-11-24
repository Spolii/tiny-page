#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "Scene.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// What the Inspector shows for a script
struct ScriptInfo {
    std::vector<Var> defaults;   // the public variables the script declares, in file order
    std::string      error;      // empty when the script loaded fine
};

// Runs the Lua script attached to each object while the editor is playing.
// A script is a .lua file with optional parts:
//   public("speed", 60)         -- a variable shown in the Inspector (number, boolean or text)
//   function start(self)        -- once, when Play starts
//   function update(self, dt)   -- every frame, dt = seconds since the last frame
// `self` has x, y, rotation, scale_x, scale_y, name, visible, flip_x, flip_y, z_index, alpha,
// look_x, look_y (read-only) and every public variable. Changes are copied back to the object.
// Every object gets its own copy of its script, so state in `self` or in globals is per object.
class Scripting {
public:
    ~Scripting() { stop(); }

    // The public variables of a script, read without playing (cached until the file changes)
    const ScriptInfo& info(const std::string& folder, const std::string& script, fs::file_time_type stamp) {
        CacheEntry& entry = infoCache[script];
        if (!entry.valid || entry.stamp != stamp) {
            entry.info  = inspect(folder + "/" + script);
            entry.stamp = stamp;
            entry.valid = true;
        }
        return entry.info;
    }

    void start(Scene& scene, const std::string& folder) {
        stop();
        L = luaL_newstate();
        luaL_openlibs(L);
        lua_register(L, "log", logFn);
        lua_register(L, "key_down", keyDownFn);
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, publicFn, 1);
        lua_setglobal(L, "public");

        instances.assign(scene.entities.size(), Instance{});
        for (size_t i = 0; i < instances.size(); ++i)
            if (!scene.entities[i].script.empty()) load(instances[i], scene.entities[i], folder);
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

    struct CacheEntry {
        bool               valid = false;
        fs::file_time_type stamp{};
        ScriptInfo         info;
    };

    // The Lua C functions below never keep objects with destructors alive across a Lua error,
    // because a Lua error jumps out of the function (longjmp).

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

    // public("name", default) while playing: put the object's value (or the default) on `self`
    static int publicFn(lua_State* L) {
        auto*       s    = static_cast<Scripting*>(lua_touserdata(L, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(L, 1);
        const int   type = lua_type(L, 2);
        if (type != LUA_TNUMBER && type != LUA_TBOOLEAN && type != LUA_TSTRING)
            return luaL_error(L, "public(\"%s\"): use a number, true/false or text", name);
        if (s->loadingSelf == LUA_NOREF) return 0;

        lua_rawgeti(L, LUA_REGISTRYINDEX, s->loadingSelf);   // [name, default, self]
        const Var* v = s->loadingVars ? findVar(*s->loadingVars, name) : nullptr;
        if (v && v->type == 'n' && type == LUA_TNUMBER)       lua_pushnumber(L, v->number);
        else if (v && v->type == 'b' && type == LUA_TBOOLEAN) lua_pushboolean(L, v->number != 0);
        else if (v && v->type == 's' && type == LUA_TSTRING)  lua_pushlstring(L, v->text.data(), v->text.size());
        else                                                  lua_pushvalue(L, 2);
        lua_setfield(L, -2, name);
        return 0;
    }

    // public("name", default) while only inspecting a script: remember the declaration
    static int declareFn(lua_State* L) {
        auto*       defs = static_cast<std::vector<Var>*>(lua_touserdata(L, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(L, 1);
        const int   type = lua_type(L, 2);
        if (type != LUA_TNUMBER && type != LUA_TBOOLEAN && type != LUA_TSTRING)
            return luaL_error(L, "public(\"%s\"): use a number, true/false or text", name);

        Var v;
        v.name = name;
        if (type == LUA_TNUMBER)       { v.type = 'n'; v.number = float(lua_tonumber(L, 2)); }
        else if (type == LUA_TBOOLEAN) { v.type = 'b'; v.number = lua_toboolean(L, 2) ? 1.0f : 0.0f; }
        else                           { v.type = 's'; v.text = lua_tostring(L, 2); }
        defs->push_back(std::move(v));
        return 0;
    }

    static int noopFn(lua_State*) { return 0; }

    static int falseFn(lua_State* L) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Run a script's top level in a throwaway Lua state, just to see what it declares.
    // Only the basic libraries are loaded, so merely selecting a script cannot touch files.
    static ScriptInfo inspect(const std::string& path) {
        ScriptInfo out;
        lua_State* L = luaL_newstate();

        const luaL_Reg libs[] = {{"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
                                 {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                 {nullptr, nullptr}};
        for (const luaL_Reg* lib = libs; lib->func; ++lib) {
            luaL_requiref(L, lib->name, lib->func, 1);
            lua_pop(L, 1);
        }

        lua_pushlightuserdata(L, &out.defaults);
        lua_pushcclosure(L, declareFn, 1);
        lua_setglobal(L, "public");
        lua_register(L, "log", noopFn);
        lua_register(L, "key_down", falseFn);

        if (luaL_dofile(L, path.c_str())) {
            const char* msg = lua_tostring(L, -1);
            out.error = msg ? msg : "unknown error";
            out.defaults.clear();
        }
        lua_close(L);
        return out;
    }

    // Pops the error message that is on top of the stack and logs it
    void report(const std::string& label) {
        const char* msg = lua_tostring(L, -1);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Script %s: %s", label.c_str(), msg ? msg : "unknown error");
        lua_pop(L, 1);
    }

    void load(Instance& in, const Entity& e, const std::string& folder) {
        if (luaL_loadfile(L, (folder + "/" + e.script).c_str()) != LUA_OK) { report(e.script); in.failed = true; return; }

        lua_newtable(L);                      // `self`: the public variables are put here while the file runs
        in.self = luaL_ref(L, LUA_REGISTRYINDEX);

        lua_newtable(L);                      // the script's environment ...
        lua_newtable(L);
        lua_pushglobaltable(L);
        lua_setfield(L, -2, "__index");       // ... which falls back to the normal globals
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        in.env = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_setupvalue(L, -2, 1);             // the file runs inside that environment

        loadingSelf = in.self;
        loadingVars = &e.vars;
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) { report(e.script); in.failed = true; }
        loadingSelf = LUA_NOREF;
        loadingVars = nullptr;
    }

    void setNumber(const char* key, float v) {
        lua_pushnumber(L, v);
        lua_setfield(L, -2, key);
    }

    void setBool(const char* key, bool v) {
        lua_pushboolean(L, v);
        lua_setfield(L, -2, key);
    }

    float getNumber(const char* key, float fallback) {
        lua_getfield(L, -1, key);
        const float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : fallback;
        lua_pop(L, 1);
        return v;
    }

    bool getBool(const char* key, bool fallback) {
        lua_getfield(L, -1, key);
        const bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
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
        setBool("visible", e.visible);
        setBool("flip_x", e.flipX);
        setBool("flip_y", e.flipY);
        setNumber("z_index", float(e.zIndex));
        setNumber("alpha", e.tint[3]);
        const SDL_FPoint look = Scene::look(e);
        setNumber("look_x", look.x);
        setNumber("look_y", look.y);
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
            e.visible  = getBool("visible", e.visible);
            e.flipX    = getBool("flip_x", e.flipX);
            e.flipY    = getBool("flip_y", e.flipY);
            e.zIndex   = int(std::lround(getNumber("z_index", float(e.zIndex))));
            e.tint[3]  = std::clamp(getNumber("alpha", e.tint[3]), 0.0f, 1.0f);
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 2);                                // env, self
    }

    lua_State*                    L = nullptr;
    std::vector<Instance>         instances;   // one per object, same order as the scene
    std::map<std::string, CacheEntry> infoCache;

    int                           loadingSelf = LUA_NOREF;   // set only while a script file is running
    const std::vector<Var>*       loadingVars = nullptr;
};