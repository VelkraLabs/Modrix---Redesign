#include "tlmod/runtime/log.h"

#include <atomic>
#include <cstdio>

namespace tlmod::rt {
namespace {

void StderrSink(TLModLogLevel level, const std::string& source, const std::string& msg) {
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    int idx = (level >= TLMOD_LOG_DEBUG && level <= TLMOD_LOG_ERROR) ? level : TLMOD_LOG_INFO;
    std::fprintf(stderr, "[TLMod] %s [%s] %s\n", kLevel[idx], source.c_str(), msg.c_str());
}

std::atomic<LogSink> g_sink{&StderrSink};

}  // namespace

void SetLogSink(LogSink sink) { g_sink.store(sink ? sink : &StderrSink); }

void Log(TLModLogLevel level, const std::string& source, const std::string& msg) {
    g_sink.load()(level, source, msg);
}

}  // namespace tlmod::rt
