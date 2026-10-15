#pragma once

#include <optional>
#include <string>
#include <vector>

namespace modrix::rt {

// Versão semântica MAJOR.MINOR.PATCH[-pre]. Metadados de build (+...) são ignorados.
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string pre;  // vazio = release

    static std::optional<SemVer> Parse(const std::string& text);
    std::string ToString() const;
    int Compare(const SemVer& other) const;  // <0, 0, >0
    bool operator==(const SemVer& o) const { return Compare(o) == 0; }
    bool operator<(const SemVer& o) const { return Compare(o) < 0; }
};

// Faixa de versões aceitas por uma dependência, estilo npm/cargo:
//   "*"              qualquer
//   "1.2.3" "=1.2.3" exata
//   ">=1.2 <2"       comparadores separados por espaço (todos precisam valer)
//   "^1.2.3"         >=1.2.3 <2.0.0   (com major 0: <0.(minor+1).0)
//   "~1.2.3"         >=1.2.3 <1.3.0
class VersionReq {
public:
    static std::optional<VersionReq> Parse(const std::string& text);
    bool Matches(const SemVer& v) const;
    const std::string& Text() const { return text_; }

private:
    enum class Op { Eq, Gt, Ge, Lt, Le };
    struct Cmp {
        Op op;
        SemVer v;
    };
    std::vector<Cmp> cmps_;
    std::string text_;
};

}  // namespace modrix::rt
