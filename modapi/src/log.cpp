#include "modrix/runtime/log.h"

#include <atomic>
#include <cstdio>

namespace modrix::rt {
namespace {

void StderrSink(ModrixLogLevel level, const std::string& source, const std::string& msg) {
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    int idx = (level >= MODRIX_LOG_DEBUG && level <= MODRIX_LOG_ERROR) ? level : MODRIX_LOG_INFO;
    std::fprintf(stderr, "[Modrix] %s [%s] %s\n", kLevel[idx], source.c_str(), msg.c_str());
}

std::atomic<LogSink> g_sink{&StderrSink};

}  // namespace

void SetLogSink(LogSink sink) { g_sink.store(sink ? sink : &StderrSink); }

void Log(ModrixLogLevel level, const std::string& source, const std::string& msg) {
    g_sink.load()(level, source, msg);
}

}  // namespace modrix::rt
