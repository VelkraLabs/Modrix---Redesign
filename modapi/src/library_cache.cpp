#include "tlmod/runtime/library_cache.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "sha256.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tlmod::rt {
namespace {

constexpr fs::perms kReadOnly = fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;

std::string Errno(const std::string& what) { return what + ": " + std::strerror(errno); }

bool IsReadOnly(const fs::path& p) {
    std::error_code ec;
    fs::perms perms = fs::status(p, ec).permissions();
    return !ec && (perms & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) ==
                      fs::perms::none;
}

// Copia `src` para `dst` (novo), calculando o SHA-256 do que foi lido.
bool CopyHashing(const fs::path& src, const fs::path& dst, std::string* sha, std::string* error) {
    std::FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) {
        *error = Errno("não foi possível ler " + src.string());
        return false;
    }
#if defined(_WIN32)
    std::FILE* out = std::fopen(dst.string().c_str(), "wbx");
#else
    int fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    std::FILE* out = fd >= 0 ? ::fdopen(fd, "wb") : nullptr;
    if (!out && fd >= 0) ::close(fd);
#endif
    if (!out) {
        *error = Errno("não foi possível criar " + dst.string());
        std::fclose(in);
        return false;
    }
    Sha256 h;
    char buf[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
        h.Update(buf, n);
        if (std::fwrite(buf, 1, n, out) != n) {
            *error = Errno("erro gravando " + dst.string());
            ok = false;
            break;
        }
    }
    if (ok && std::ferror(in)) {
        *error = "erro lendo " + src.string();
        ok = false;
    }
    if (ok && std::fflush(out) != 0) {
        *error = Errno("erro gravando " + dst.string());
        ok = false;
    }
#if !defined(_WIN32)
    if (ok && ::fsync(::fileno(out)) != 0) {
        *error = Errno("fsync de " + dst.string());
        ok = false;
    }
#endif
    std::fclose(in);
    if (std::fclose(out) != 0 && ok) {
        *error = Errno("erro fechando " + dst.string());
        ok = false;
    }
    if (ok) *sha = h.HexDigest();
    return ok;
}

}  // namespace

std::optional<LibraryCache::Result> LibraryCache::Prepare(const std::string& modId, const fs::path& source,
                                                          std::string* error) const {
    std::string err;
    auto fail = [&](std::string why) -> std::optional<Result> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };

    std::optional<std::string> srcSha = Sha256File(source, &err);
    if (!srcSha) return fail(err);

    const fs::path dir = root_ / modId;
    const fs::path dest = dir / source.filename();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || !fs::is_directory(dir, ec)) return fail("não foi possível criar " + dir.string() + ": " + ec.message());

    // Sobras de uma cópia interrompida (outro processo, mesma pasta).
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind(source.filename().string() + ".tmp-", 0) == 0) fs::remove(e.path(), ec);
    }

    // Reaproveita a cópia que já confere.
    if (fs::is_regular_file(dest, ec)) {
        std::optional<std::string> destSha = Sha256File(dest, nullptr);
        if (destSha && *destSha == *srcSha) {
            if (!IsReadOnly(dest)) {
                fs::permissions(dest, kReadOnly, fs::perm_options::replace, ec);
                if (ec) return fail("não foi possível marcar " + dest.string() + " como somente leitura: " + ec.message());
            }
            return Result{dest, false, *srcSha};
        }
    }

    static std::atomic<unsigned> counter{0};
#if defined(_WIN32)
    const unsigned long pid = 0;
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    const fs::path tmp = dir / (source.filename().string() + ".tmp-" + std::to_string(pid) + "-" +
                                std::to_string(counter.fetch_add(1)));
    std::string copiedSha;
    if (!CopyHashing(source, tmp, &copiedSha, &err)) {
        fs::remove(tmp, ec);
        return fail(err);
    }
    if (afterWriteForTest) afterWriteForTest(tmp);

    if (copiedSha != *srcSha) {
        fs::remove(tmp, ec);
        return fail("a origem mudou durante a cópia (SHA-256 " + *srcSha + " antes, " + copiedSha + " durante)");
    }
    std::optional<std::string> tmpSha = Sha256File(tmp, &err);
    if (!tmpSha || *tmpSha != *srcSha) {
        fs::remove(tmp, ec);
        return fail(tmpSha ? "a cópia não confere com a origem (SHA-256 " + *tmpSha + ", esperado " + *srcSha + ")"
                           : err);
    }
    fs::permissions(tmp, kReadOnly, fs::perm_options::replace, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail("não foi possível marcar a cópia como somente leitura: " + ec.message());
    }
    fs::rename(tmp, dest, ec);  // atômico na mesma pasta; substitui a cópia antiga
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        return fail("não foi possível mover a cópia para " + dest.string() + ": " + ec.message());
    }
    return Result{dest, true, *srcSha};
}

}  // namespace tlmod::rt
