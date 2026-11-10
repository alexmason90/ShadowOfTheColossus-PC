// bbport: see log.h.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include "common/assert_scope.h"
#include "common/logging/log.h"

namespace Common::Log {
static Level threshold() {
    static const Level level = [] {
        const char* v = std::getenv("BB_GPU_LOG");
        if (!v) return Level::warn;
        static const char* names[] = {"trace", "debug", "info", "warning", "error", "critical"};
        for (int i = 0; i < 6; ++i)
            if (!std::strcmp(v, names[i])) return Level(i);
        return Level::warn;
    }();
    return level;
}
bool ShouldLog(Level level) { return level >= threshold(); }
void Write(std::string_view log_class, Level level, const char* file, int line, const char* function,
           std::string&& message) {
    static std::mutex mutex;
    static const char* names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
    const char* base = std::strrchr(file, '/');
    // sotc: an assertion on a pipeline worker (Common::g_assert_throws) is not fatal: the worker
    // read guest tables the game may already have reused, and the command processor translates
    // that shader itself (vk_pipeline_cache.cpp RunAsyncTranslate). Logged as a warning so that
    // "<Critical>" keeps meaning an assertion that stops the port.
    const bool handed_back = level == Level::critical && Common::g_assert_throws;
    std::scoped_lock lock{mutex};
    std::fprintf(stderr, "GPU [%.*s] <%s> %s:%d %s: %s%s\n", int(log_class.size()), log_class.data(),
                 handed_back ? "Warning" : names[int(level)], base ? base + 1 : file, line,
                 function, handed_back ? "(translation worker, shader handed back) " : "",
                 message.c_str());
}
} // namespace Common::Log
