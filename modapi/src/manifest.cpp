#include "tlmod/runtime/manifest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "json.h"

namespace tlmod::rt {
namespace {

const char* const kKindKeys[TLMOD_CONTENT_KIND_COUNT] = {"items", "npcs",  "projectiles",
                                                         "tiles", "walls", "buffs"};

bool Fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

bool GetString(const json::Value& root, const char* key, bool required, std::string& out,
               std::string* error) {
    const json::Value* v = root.Get(key);
    if (!v) return required ? Fail(error, std::string(key) + ": obrigatório") : true;
    const std::string* s = v->AsString();
    if (!s) return Fail(error, std::string(key) + ": deve ser string");
    out = *s;
    return true;
}

bool ParseDeps(const json::Value& root, const char* key, bool optional, ModManifest& m,
               std::string* error) {
    const json::Value* v = root.Get(key);
    if (!v) return true;
    const json::Object* obj = v->AsObject();
    if (!obj) return Fail(error, std::string(key) + ": deve ser objeto {\"mod_id\": \"faixa\"}");
    for (const auto& [id, reqVal] : *obj) {
        std::string where = std::string(key) + "." + id;
        if (!IsValidModId(id)) return Fail(error, where + ": id de mod inválido");
        if (id == m.id) return Fail(error, where + ": o mod não pode depender de si mesmo");
        for (const Dependency& d : m.dependencies)
            if (d.id == id) return Fail(error, where + ": dependência repetida");
        const std::string* reqText = reqVal.AsString();
        if (!reqText) return Fail(error, where + ": a faixa de versão deve ser string");
        auto req = VersionReq::Parse(*reqText);
        if (!req) return Fail(error, where + ": faixa de versão inválida \"" + *reqText + "\"");
        m.dependencies.push_back({id, *req, optional});
    }
    return true;
}

}  // namespace

bool IsSafeRelativePath(const std::string& p) {
    if (p.empty()) return false;
    std::filesystem::path path(p);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path)
        if (part == "..") return false;
    return true;
}

bool IsValidModId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    bool prevSep = true;  // não pode começar com separador
    for (char c : id) {
        bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        bool sep = c == '.' || c == '_' || c == '-';
        if (!alnum && !sep) return false;
        if (sep && prevSep) return false;
        prevSep = sep;
    }
    return !prevSep;
}

bool IsValidContentName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

const char* ContentKindKey(TLModContentKind kind) {
    return (kind >= 0 && kind < TLMOD_CONTENT_KIND_COUNT) ? kKindKeys[kind] : "?";
}

const char* CurrentPlatformKey() {
#if defined(__ANDROID__) && defined(__aarch64__)
    return "android-arm64";
#elif defined(__ANDROID__) && defined(__arm__)
    return "android-arm";
#elif defined(__ANDROID__) && defined(__x86_64__)
    return "android-x86_64";
#elif defined(_WIN32) && defined(_M_X64)
    return "windows-x86_64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "unknown";
#endif
}

bool ModManifest::Declares(TLModContentKind kind, const std::string& n) const {
    const auto& list = content[kind];
    return std::find(list.begin(), list.end(), n) != list.end();
}

std::optional<ModManifest> ParseManifest(const std::string& text, std::string* error) {
    auto root = json::Parse(text, error);
    if (!root) return std::nullopt;
    if (!root->AsObject()) {
        Fail(error, "a raiz deve ser um objeto");
        return std::nullopt;
    }

    ModManifest m;
    const json::Value* api = root->Get("tlmod_api");
    const double* apiNum = api ? api->AsNumber() : nullptr;
    if (!apiNum || *apiNum < 1 || *apiNum != std::floor(*apiNum) || *apiNum > 1000) {
        Fail(error, "tlmod_api: obrigatório, inteiro >= 1");
        return std::nullopt;
    }
    m.apiVersion = static_cast<uint32_t>(*apiNum);

    if (!GetString(*root, "id", true, m.id, error)) return std::nullopt;
    if (!IsValidModId(m.id)) {
        Fail(error, "id: \"" + m.id + "\" inválido (use [a-z0-9] separados por . _ -, até 64)");
        return std::nullopt;
    }
    if (!GetString(*root, "name", true, m.name, error)) return std::nullopt;
    if (m.name.empty()) {
        Fail(error, "name: não pode ser vazio");
        return std::nullopt;
    }
    std::string ver;
    if (!GetString(*root, "version", true, ver, error)) return std::nullopt;
    auto sv = SemVer::Parse(ver);
    if (!sv) {
        Fail(error, "version: \"" + ver + "\" não é semver (MAJOR.MINOR.PATCH)");
        return std::nullopt;
    }
    m.version = *sv;
    if (!GetString(*root, "description", false, m.description, error)) return std::nullopt;

    if (const json::Value* a = root->Get("authors")) {
        const json::Array* arr = a->AsArray();
        if (!arr) {
            Fail(error, "authors: deve ser lista de strings");
            return std::nullopt;
        }
        for (const auto& item : *arr) {
            const std::string* s = item.AsString();
            if (!s) {
                Fail(error, "authors: deve ser lista de strings");
                return std::nullopt;
            }
            m.authors.push_back(*s);
        }
    }

    if (!ParseDeps(*root, "dependencies", false, m, error)) return std::nullopt;
    if (!ParseDeps(*root, "optional_dependencies", true, m, error)) return std::nullopt;

    if (const json::Value* c = root->Get("content")) {
        const json::Object* obj = c->AsObject();
        if (!obj) {
            Fail(error, "content: deve ser objeto");
            return std::nullopt;
        }
        for (const auto& [kindKey, list] : *obj) {
            int kind = -1;
            for (int k = 0; k < TLMOD_CONTENT_KIND_COUNT; ++k)
                if (kindKey == kKindKeys[k]) kind = k;
            if (kind < 0) {
                Fail(error, "content." + kindKey + ": tipo desconhecido");
                return std::nullopt;
            }
            const json::Array* arr = list.AsArray();
            if (!arr) {
                Fail(error, "content." + kindKey + ": deve ser lista de nomes");
                return std::nullopt;
            }
            std::set<std::string> seen;
            for (const auto& item : *arr) {
                const std::string* s = item.AsString();
                if (!s || !IsValidContentName(*s)) {
                    Fail(error, "content." + kindKey + ": nomes devem casar [a-z0-9_]{1,64}");
                    return std::nullopt;
                }
                if (!seen.insert(*s).second) {
                    Fail(error, "content." + kindKey + ": \"" + *s + "\" repetido");
                    return std::nullopt;
                }
                m.content[kind].push_back(*s);
            }
        }
    }

    if (const json::Value* p = root->Get("provides")) {
        const json::Array* arr = p->AsArray();
        if (!arr) {
            Fail(error, "provides: deve ser lista de nomes de canal");
            return std::nullopt;
        }
        for (const auto& item : *arr) {
            const std::string* s = item.AsString();
            if (!s || !IsValidContentName(*s)) {
                Fail(error, "provides: canais devem casar [a-z0-9_]{1,64}");
                return std::nullopt;
            }
            if (std::find(m.provides.begin(), m.provides.end(), *s) != m.provides.end()) {
                Fail(error, "provides: \"" + *s + "\" repetido");
                return std::nullopt;
            }
            m.provides.push_back(*s);
        }
    }

    const json::Value* entry = root->Get("entry");
    const json::Object* entryObj = entry ? entry->AsObject() : nullptr;
    if (!entryObj || entryObj->empty()) {
        Fail(error, "entry: obrigatório, objeto {\"plataforma\": \"lib/.../biblioteca\"}");
        return std::nullopt;
    }
    for (const auto& [platform, pathVal] : *entryObj) {
        const std::string* p = pathVal.AsString();
        if (!p || !IsSafeRelativePath(*p)) {
            Fail(error, "entry." + platform + ": caminho relativo à pasta do mod, sem \"..\"");
            return std::nullopt;
        }
        m.entries[platform] = *p;
    }
    return m;
}

std::optional<ModManifest> LoadManifest(const std::filesystem::path& modDir, std::string* error) {
    std::ifstream in(modDir / "manifest.json", std::ios::binary);
    if (!in) {
        Fail(error, "manifest.json não encontrado");
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto m = ParseManifest(ss.str(), error);
    if (m) m->dir = modDir;
    return m;
}

}  // namespace tlmod::rt
