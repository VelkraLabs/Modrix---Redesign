// Parser JSON mínimo (RFC 8259) para manifest.json. Interno ao runtime.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace tlmod::rt::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;  // mantém a ordem do arquivo

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Array>,
                 std::shared_ptr<Object>>
        v;

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(v); }
    const bool* AsBool() const { return std::get_if<bool>(&v); }
    const double* AsNumber() const { return std::get_if<double>(&v); }
    const std::string* AsString() const { return std::get_if<std::string>(&v); }
    const Array* AsArray() const {
        auto p = std::get_if<std::shared_ptr<Array>>(&v);
        return p ? p->get() : nullptr;
    }
    const Object* AsObject() const {
        auto p = std::get_if<std::shared_ptr<Object>>(&v);
        return p ? p->get() : nullptr;
    }
    const Value* Get(const std::string& key) const;  // só para objetos
};

// Chaves duplicadas num objeto são erro (evita manifests ambíguos).
std::optional<Value> Parse(const std::string& text, std::string* error);

}  // namespace tlmod::rt::json
