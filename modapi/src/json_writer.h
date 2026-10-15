// Escritor JSON mínimo (UTF-8 passa direto; escapa aspas, barra e controles).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tlmod::rt {

class JsonWriter {
public:
    JsonWriter& BeginObject() { Sep(); out_ += '{'; first_.push_back(true); return *this; }
    JsonWriter& EndObject() { out_ += '}'; first_.pop_back(); return *this; }
    JsonWriter& BeginArray() { Sep(); out_ += '['; first_.push_back(true); return *this; }
    JsonWriter& EndArray() { out_ += ']'; first_.pop_back(); return *this; }

    JsonWriter& Key(const std::string& k) {
        Sep();
        Quote(k);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }
    JsonWriter& String(const std::string& v) { Sep(); Quote(v); return *this; }
    JsonWriter& Bool(bool v) { Sep(); out_ += v ? "true" : "false"; return *this; }
    JsonWriter& Int(int64_t v) { Sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& Null() { Sep(); out_ += "null"; return *this; }

    const std::string& str() const { return out_; }

private:
    void Sep() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back()) out_ += ',';
            first_.back() = false;
        }
    }
    void Quote(const std::string& s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out_ += buf;
                    } else {
                        out_ += static_cast<char>(c);  // UTF-8 (acentos) passa direto
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> first_;
    bool afterKey_ = false;
};

}  // namespace tlmod::rt
