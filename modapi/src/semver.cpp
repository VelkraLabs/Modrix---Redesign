#include "modrix/runtime/semver.h"

#include <cctype>
#include <sstream>

namespace modrix::rt {
namespace {

bool ParseUint(const std::string& s, size_t& i, int& out) {
    size_t b = i;
    long long v = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        v = v * 10 + (s[i] - '0');
        if (v > 1000000000) return false;
        ++i;
    }
    if (i == b) return false;
    if (i - b > 1 && s[b] == '0') return false;  // sem zeros à esquerda
    out = static_cast<int>(v);
    return true;
}

// Versão parcial ("1", "1.2") para faixas: componentes ausentes viram 0.
std::optional<SemVer> ParsePartial(const std::string& s, int* parts) {
    SemVer v;
    size_t i = 0;
    int* fields[] = {&v.major, &v.minor, &v.patch};
    int n = 0;
    while (n < 3) {
        if (!ParseUint(s, i, *fields[n])) return std::nullopt;
        ++n;
        if (i < s.size() && s[i] == '.') {
            ++i;
            continue;
        }
        break;
    }
    if (i < s.size() && s[i] == '-') {
        if (n != 3) return std::nullopt;
        size_t end = s.find('+', i);
        v.pre = s.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
        if (v.pre.empty()) return std::nullopt;
        i = end == std::string::npos ? s.size() : end;
    }
    if (i < s.size() && s[i] == '+') i = s.size();  // metadados de build: ignorados
    if (i != s.size()) return std::nullopt;
    if (parts) *parts = n;
    return v;
}

}  // namespace

std::optional<SemVer> SemVer::Parse(const std::string& text) {
    int parts = 0;
    auto v = ParsePartial(text, &parts);
    if (!v || parts != 3) return std::nullopt;
    return v;
}

std::string SemVer::ToString() const {
    std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    if (!pre.empty()) s += "-" + pre;
    return s;
}

int SemVer::Compare(const SemVer& o) const {
    if (major != o.major) return major < o.major ? -1 : 1;
    if (minor != o.minor) return minor < o.minor ? -1 : 1;
    if (patch != o.patch) return patch < o.patch ? -1 : 1;
    if (pre == o.pre) return 0;
    if (pre.empty()) return 1;  // release > pré-release
    if (o.pre.empty()) return -1;
    return pre < o.pre ? -1 : 1;
}

std::optional<VersionReq> VersionReq::Parse(const std::string& text) {
    VersionReq r;
    r.text_ = text;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        if (tok == "*") continue;
        size_t opLen = 0;
        std::string op;
        for (const char* cand : {">=", "<=", ">", "<", "=", "^", "~"}) {
            size_t n = std::char_traits<char>::length(cand);
            if (tok.compare(0, n, cand) == 0) {
                op = cand;
                opLen = n;
                break;
            }
        }
        int parts = 0;
        auto v = ParsePartial(tok.substr(opLen), &parts);
        if (!v) return std::nullopt;
        if (op == "^") {
            SemVer upper;
            if (v->major > 0 || parts == 1) upper = {v->major + 1, 0, 0, ""};
            else if (v->minor > 0 || parts == 2) upper = {0, v->minor + 1, 0, ""};
            else upper = {0, 0, v->patch + 1, ""};
            r.cmps_.push_back({Op::Ge, *v});
            r.cmps_.push_back({Op::Lt, upper});
        } else if (op == "~") {
            SemVer upper = parts == 1 ? SemVer{v->major + 1, 0, 0, ""} : SemVer{v->major, v->minor + 1, 0, ""};
            r.cmps_.push_back({Op::Ge, *v});
            r.cmps_.push_back({Op::Lt, upper});
        } else if (op.empty() || op == "=") {
            if (parts == 3) {
                r.cmps_.push_back({Op::Eq, *v});
            } else {  // "1.2" = qualquer 1.2.x
                SemVer upper = parts == 1 ? SemVer{v->major + 1, 0, 0, ""} : SemVer{v->major, v->minor + 1, 0, ""};
                r.cmps_.push_back({Op::Ge, *v});
                r.cmps_.push_back({Op::Lt, upper});
            }
        } else {
            Op o = op == ">=" ? Op::Ge : op == "<=" ? Op::Le : op == ">" ? Op::Gt : Op::Lt;
            r.cmps_.push_back({o, *v});
        }
    }
    return r;
}

bool VersionReq::Matches(const SemVer& v) const {
    for (const Cmp& c : cmps_) {
        int k = v.Compare(c.v);
        bool ok = false;
        switch (c.op) {
            case Op::Eq: ok = k == 0; break;
            case Op::Gt: ok = k > 0; break;
            case Op::Ge: ok = k >= 0; break;
            case Op::Lt: ok = k < 0; break;
            case Op::Le: ok = k <= 0; break;
        }
        if (!ok) return false;
    }
    return true;
}

}  // namespace modrix::rt
