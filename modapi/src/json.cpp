#include "json.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tlmod::rt::json {

const Value* Value::Get(const std::string& key) const {
    const Object* o = AsObject();
    if (!o) return nullptr;
    for (const auto& kv : *o)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    std::optional<Value> ParseDocument(std::string* error) {
        SkipWs();
        Value v;
        if (!ParseValue(v, 0)) return Fail(error);
        SkipWs();
        if (i_ != s_.size()) {
            err_ = "conteúdo extra após o valor";
            return Fail(error);
        }
        return v;
    }

private:
    static constexpr int kMaxDepth = 64;

    std::optional<Value> Fail(std::string* error) {
        if (error) *error = "JSON inválido na posição " + std::to_string(i_) + ": " + err_;
        return std::nullopt;
    }

    void SkipWs() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r'))
            ++i_;
    }

    bool Literal(const char* lit) {
        size_t n = std::strlen(lit);
        if (s_.compare(i_, n, lit) != 0) return false;
        i_ += n;
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            err_ = "aninhamento profundo demais";
            return false;
        }
        if (i_ >= s_.size()) {
            err_ = "fim inesperado";
            return false;
        }
        char c = s_[i_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!ParseString(str)) return false;
            out.v = std::move(str);
            return true;
        }
        if (Literal("true")) { out.v = true; return true; }
        if (Literal("false")) { out.v = false; return true; }
        if (Literal("null")) { out.v = nullptr; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        err_ = std::string("caractere inesperado '") + c + "'";
        return false;
    }

    bool ParseObject(Value& out, int depth) {
        auto obj = std::make_shared<Object>();
        ++i_;  // {
        SkipWs();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            out.v = obj;
            return true;
        }
        while (true) {
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != '"') {
                err_ = "esperava chave entre aspas";
                return false;
            }
            std::string key;
            if (!ParseString(key)) return false;
            for (const auto& kv : *obj) {
                if (kv.first == key) {
                    err_ = "chave duplicada \"" + key + "\"";
                    return false;
                }
            }
            SkipWs();
            if (i_ >= s_.size() || s_[i_] != ':') {
                err_ = "esperava ':'";
                return false;
            }
            ++i_;
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            obj->emplace_back(std::move(key), std::move(v));
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; break; }
            err_ = "esperava ',' ou '}'";
            return false;
        }
        out.v = obj;
        return true;
    }

    bool ParseArray(Value& out, int depth) {
        auto arr = std::make_shared<Array>();
        ++i_;  // [
        SkipWs();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            out.v = arr;
            return true;
        }
        while (true) {
            SkipWs();
            Value v;
            if (!ParseValue(v, depth + 1)) return false;
            arr->push_back(std::move(v));
            SkipWs();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; break; }
            err_ = "esperava ',' ou ']'";
            return false;
        }
        out.v = arr;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(uint32_t& cp) {
        if (i_ + 4 > s_.size()) {
            err_ = "\\u incompleto";
            return false;
        }
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            char h = s_[i_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<uint32_t>(h - 'A' + 10);
            else {
                err_ = "\\u inválido";
                return false;
            }
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++i_;  // "
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) {
                err_ = "caractere de controle em string";
                return false;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) break;
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // par substituto
                        uint32_t lo;
                        if (!Literal("\\u") || !Hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) {
                            err_ = "par substituto UTF-16 inválido";
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    err_ = "escape inválido";
                    return false;
            }
        }
        err_ = "string não terminada";
        return false;
    }

    bool ParseNumber(Value& out) {
        size_t start = i_;
        if (s_[i_] == '-') ++i_;
        auto digits = [&] {
            size_t b = i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
            return i_ > b;
        };
        if (!digits()) { err_ = "número inválido"; return false; }
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            if (!digits()) { err_ = "número inválido"; return false; }
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (!digits()) { err_ = "número inválido"; return false; }
        }
        out.v = std::strtod(s_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }

    const std::string& s_;
    size_t i_ = 0;
    std::string err_;
};

}  // namespace

std::optional<Value> Parse(const std::string& text, std::string* error) {
    return Parser(text).ParseDocument(error);
}

}  // namespace tlmod::rt::json
