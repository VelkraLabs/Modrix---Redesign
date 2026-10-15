#include "tlmod/overlay/config_store.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"
#include "json_writer.h"

namespace tlmod::overlay {

bool ConfigStore::Load(std::string* error) {
    disabled_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) return true;  // primeira execução

    std::ifstream in(file_, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string parseError;
    auto root = rt::json::Parse(ss.str(), &parseError);
    const rt::json::Value* list = root ? root->Get("disabled_mods") : nullptr;
    if (!root || !root->AsObject() || (list && !list->AsArray())) {
        if (error) *error = file_.string() + ": configuração inválida (" + (root ? "disabled_mods" : parseError) + ")";
        return false;
    }
    if (list) {
        for (const auto& v : *list->AsArray())
            if (const std::string* id = v.AsString()) disabled_.insert(*id);
    }
    return true;
}

bool ConfigStore::SetEnabled(const std::string& modId, bool enabled, std::string* error) {
    std::set<std::string> next = disabled_;
    if (enabled) next.erase(modId);
    else next.insert(modId);
    if (next == disabled_) return true;
    if (!Save(next, error)) return false;
    disabled_ = std::move(next);
    return true;
}

bool ConfigStore::Save(const std::set<std::string>& disabled, std::string* error) const {
    rt::JsonWriter w;
    w.BeginObject();
    w.Key("schema").Int(1);
    w.Key("disabled_mods").BeginArray();
    for (const std::string& id : disabled) w.String(id);
    w.EndArray();
    w.EndObject();

    std::error_code ec;
    if (file_.has_parent_path()) std::filesystem::create_directories(file_.parent_path(), ec);
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << w.str() << '\n';
        out.flush();
        if (!out) {
            if (error) *error = "não foi possível gravar " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, file_, ec);  // atômico no mesmo sistema de arquivos
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "não foi possível salvar " + file_.string();
        return false;
    }
    return true;
}

}  // namespace tlmod::overlay
