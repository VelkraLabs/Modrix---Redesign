#include "tlmod/runtime/log.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <system_error>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace tlmod::rt {
namespace {

void StderrSink(TLModLogLevel level, const std::string& source, const std::string& msg) {
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    int idx = (level >= TLMOD_LOG_DEBUG && level <= TLMOD_LOG_ERROR) ? level : TLMOD_LOG_INFO;
    std::fprintf(stderr, "[TLMod] %s [%s] %s\n", kLevel[idx], source.c_str(), msg.c_str());
}

std::atomic<LogSink> g_sink{&StderrSink};

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Memória + arquivo, sob um mutex só (a ordem no arquivo é a ordem de chegada).
struct Book {
    std::mutex mutex;
    std::deque<LogEntry> lines;
    uint64_t total = 0;        // linhas registradas desde o início
    uint64_t written = 0;      // até qual (total) já foi para o arquivo
    std::filesystem::path path;
    std::FILE* file = nullptr;
    uintmax_t fileBytes = 0;

    bool Open(std::string* error) {
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        file = std::fopen(path.c_str(), "ab");
        if (!file) {
            if (error) *error = "não foi possível abrir " + path.string() + ": " + std::strerror(errno);
            return false;
        }
        fileBytes = std::filesystem::file_size(path, ec);
        if (ec) fileBytes = 0;
        return true;
    }
    void Rotate() {
        std::fclose(file);
        file = nullptr;
        std::error_code ec;
        std::filesystem::path old = path;
        old += ".1";
        std::filesystem::rename(path, old, ec);
        Open(nullptr);
    }
    void Write(const std::string& text) {
        if (!file) return;
        if (fileBytes > 0 && fileBytes + text.size() + 1 > kLogFileMaxBytes) {
            Rotate();
            if (!file) return;
        }
        std::fwrite(text.data(), 1, text.size(), file);
        std::fputc('\n', file);
        std::fflush(file);  // um fechamento inesperado do jogo não perde o fim do log
        fileBytes += text.size() + 1;
    }
    void Add(LogEntry e) {
        std::lock_guard<std::mutex> lock(mutex);
        if (file) Write(e.Format());
        lines.push_back(std::move(e));
        if (lines.size() > kLogMemoryLines) lines.pop_front();
        ++total;
        if (file) written = total;
    }
};

Book& TheBook() {
    static Book* book = new Book;  // nunca destruído: mods logam até o fim do processo
    return *book;
}

}  // namespace

std::string LogEntry::Format() const {
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    int idx = (level >= TLMOD_LOG_DEBUG && level <= TLMOD_LOG_ERROR) ? level : TLMOD_LOG_INFO;
    std::time_t secs = static_cast<std::time_t>(timeMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(timeMs % 1000));
    return std::string(stamp) + " " + kLevel[idx] + " [" + source + "] " + message;
}

void SetLogSink(LogSink sink) { g_sink.store(sink ? sink : &StderrSink); }

void RecordLog(TLModLogLevel level, const std::string& source, const std::string& msg, int64_t timeMs) {
    TheBook().Add(LogEntry{timeMs > 0 ? timeMs : NowMs(), level, source, msg});
}

void Log(TLModLogLevel level, const std::string& source, const std::string& msg) {
    RecordLog(level, source, msg);
    g_sink.load()(level, source, msg);
}

std::vector<LogEntry> RecentLog(size_t max) {
    Book& b = TheBook();
    std::lock_guard<std::mutex> lock(b.mutex);
    size_t n = std::min(max, b.lines.size());
    return std::vector<LogEntry>(b.lines.end() - static_cast<std::ptrdiff_t>(n), b.lines.end());
}

bool SetLogFile(const std::filesystem::path& file, std::string* error) {
    Book& b = TheBook();
    std::lock_guard<std::mutex> lock(b.mutex);
    if (b.file) std::fclose(b.file);
    b.file = nullptr;
    b.path = file;
    if (file.empty()) return true;
    if (!b.Open(error)) {
        b.path.clear();
        return false;
    }
    char head[160];
    LogEntry now{NowMs(), TLMOD_LOG_INFO, "log", ""};
#if defined(_WIN32)
    const long pid = 0;
#else
    const long pid = static_cast<long>(::getpid());
#endif
    std::snprintf(head, sizeof(head), "===== TLMod: sessão iniciada %s (pid %ld) =====",
                  now.Format().substr(0, 23).c_str(), pid);
    b.Write(head);
    // O que chegou antes de haver arquivo e ainda está na memória.
    uint64_t pending = b.total - b.written;
    if (pending > b.lines.size()) {
        b.Write("(" + std::to_string(pending - b.lines.size()) + " linhas anteriores não couberam na memória)");
        pending = b.lines.size();
    }
    for (size_t i = b.lines.size() - static_cast<size_t>(pending); i < b.lines.size(); ++i) b.Write(b.lines[i].Format());
    b.written = b.total;
    return true;
}

std::filesystem::path LogFilePath() {
    Book& b = TheBook();
    std::lock_guard<std::mutex> lock(b.mutex);
    return b.file ? b.path : std::filesystem::path();
}

void ResetLogForTest() {
    SetLogFile({});
    Book& b = TheBook();
    std::lock_guard<std::mutex> lock(b.mutex);
    b.lines.clear();
    b.total = b.written = 0;
    g_sink.store(&StderrSink);
}

}  // namespace tlmod::rt
