#pragma once
#include <SDL3/SDL.h>
#include <cstdio>
#include <string>
#include <vector>

struct LogEntry {
    SDL_LogPriority level;
    std::string     text;
};

class Log {
public:
    static void init() { SDL_SetLogOutputFunction(&capture, nullptr); }
    static const std::vector<LogEntry>& entries() { return store(); }

    // Whole log as plain text (used for crash.log and the clipboard)
    static std::string text() {
        std::string out;
        for (const auto& e : store()) {
            out += e.level >= SDL_LOG_PRIORITY_ERROR ? "[ERROR] "
                 : e.level == SDL_LOG_PRIORITY_WARN  ? "[WARN] "
                                                     : "[INFO] ";
            out += e.text + "\n";
        }
        return out;
    }

private:
    static std::vector<LogEntry>& store() {
        static std::vector<LogEntry> s;
        return s;
    }
    static void capture(void*, int, SDL_LogPriority level, const char* msg) {
        std::puts(msg);
        store().push_back({level, msg});
    }
};