#include "log.h"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

#include "bridge.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace {

constexpr size_t kEarlyLines = 256;

struct Early {
    int level;
    int64_t timeMs;
    std::string text;
};

std::mutex g_mutex;
TLModLogCallback g_callback = nullptr;  // protegido por g_mutex
std::deque<Early>* g_early = new std::deque<Early>;  // nunca destruído: há log até o fim do processo
size_t g_earlyDropped = 0;

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

namespace bridge {

void LogLine(int level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#if defined(__ANDROID__)
    static const int kPrio[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
    __android_log_write(kPrio[level >= 0 && level <= 3 ? level : 1], "TLModBridge", buf);
#else
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    std::fprintf(stderr, "[TLModBridge] %s %s\n", kLevel[level >= 0 && level <= 3 ? level : 1], buf);
#endif
    const int64_t now = NowMs();
    TLModLogCallback cb;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        cb = g_callback;
        if (!cb) {
            g_early->push_back({level, now, buf});
            if (g_early->size() > kEarlyLines) {
                g_early->pop_front();
                ++g_earlyDropped;
            }
            return;
        }
    }
    cb(level, now, buf);
}

}  // namespace bridge

extern "C" void TLMod_SetLogCallback(TLModLogCallback cb) {
    std::deque<Early> early;
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_callback = cb;
        if (!cb) return;
        early.swap(*g_early);
        dropped = g_earlyDropped;
        g_earlyDropped = 0;
    }
    if (dropped > 0) {
        std::string note = "(" + std::to_string(dropped) + " linhas anteriores da ponte não foram guardadas)";
        cb(2, early.empty() ? NowMs() : early.front().timeMs, note.c_str());
    }
    for (const Early& e : early) cb(e.level, e.timeMs, e.text.c_str());
}
