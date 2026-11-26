#pragma once
#include <SDL3/SDL.h>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// Reads a data file: a Lua file that returns a table, like
//     return { name = "My Game", main_scene = "Main" }
// It runs with no libraries and an empty environment, so a data file can only describe data.
class LuaFile {
public:
    explicit LuaFile(const std::filesystem::path& path) {
        L = luaL_newstate();
        if (luaL_loadfile(L, path.string().c_str()) != LUA_OK) { fail(); return; }
        lua_newtable(L);
        lua_setupvalue(L, -2, 1);   // the file's globals are now an empty table
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) { fail(); return; }
        if (!lua_istable(L, -1)) { message = "the file must return a table"; return; }
        good = true;
    }
    ~LuaFile() { lua_close(L); }
    LuaFile(const LuaFile&) = delete;
    LuaFile& operator=(const LuaFile&) = delete;

    bool ok() const { return good; }
    const std::string& error() const { return message; }
    lua_State* state() const { return L; }   // the returned table is on top of its stack

private:
    void fail() {
        const char* m = lua_tostring(L, -1);
        message = m ? m : "unknown error";
    }

    lua_State*  L = nullptr;
    bool        good = false;
    std::string message;
};

// Helpers: read a field of the table on top of the stack, or write Lua source text
namespace lua_data {

inline float num(lua_State* L, const char* key, float def) {
    lua_getfield(L, -1, key);
    const float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}

inline bool flag(lua_State* L, const char* key, bool def) {
    lua_getfield(L, -1, key);
    const bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : def;
    lua_pop(L, 1);
    return v;
}

inline std::string str(lua_State* L, const char* key, const std::string& def = {}) {
    lua_getfield(L, -1, key);
    std::string v = def;
    if (lua_type(L, -1) == LUA_TSTRING) {
        size_t n = 0;
        const char* s = lua_tolstring(L, -1, &n);
        v.assign(s, n);
    }
    lua_pop(L, 1);
    return v;
}

// The shortest text that reads back as the same number
inline std::string number(float v) {
    if (!std::isfinite(v)) v = 0;
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

inline std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
        case '\\': o += "\\\\"; break;
        case '"':  o += "\\\""; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        default:   o += c;
        }
    }
    return o + "\"";
}

}  // namespace lua_data