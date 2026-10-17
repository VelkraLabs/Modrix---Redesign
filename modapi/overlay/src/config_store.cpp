#include "tlmod/overlay/config_store.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"
#include "json_writer.h"

namespace tlmod::overlay {

namespace {

int32_t Clamp(double v, int32_t lo, int32_t hi) {
    if (!std::isfinite(v)) return lo;
    return static_cast<int32_t>(std::clamp(std::lround(v), static_cast<long>(lo), static_cast<long>(hi)));
}

// "buttons" é cosmético: o que não der para ler fica no padrão.
ButtonLayout ParseButtons(const rt::json::Value* v) {
    ButtonLayout b;
    if (!v || !v->AsObject()) return b;
    if (const rt::json::Value* s = v->Get("scale"); s && s->AsNumber())
        b.scalePct = Clamp(*s->AsNumber(), ButtonLayout::kScaleMin, ButtonLayout::kScaleMax);
    if (const rt::json::Value* o = v->Get("opacity"); o && o->AsNumber())
        b.opacityPct = Clamp(*o->AsNumber(), ButtonLayout::kOpacityMin, ButtonLayout::kOpacityMax);
    const rt::json::Value* positions = v->Get("positions");
    if (!positions || !positions->AsObject()) return b;
    for (const auto& [id, pos] : *positions->AsObject()) {
        const rt::json::Array* xy = pos.AsArray();
        if (id.empty() || !xy || xy->size() != 2 || !(*xy)[0].AsNumber() || !(*xy)[1].AsNumber()) continue;
        b.positions[id] = {Clamp(*(*xy)[0].AsNumber(), 0, ButtonLayout::kPlacementMax),
                           Clamp(*(*xy)[1].AsNumber(), 0, ButtonLayout::kPlacementMax)};
    }
    return b;
}

}  // namespace

bool ConfigStore::Load(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    revision_.fetch_add(1, std::memory_order_acq_rel);
    state_ = State{};
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
            if (const std::string* id = v.AsString()) state_.disabled.insert(*id);
    }
    state_.buttons = ParseButtons(root->Get("buttons"));
    return true;
}

bool ConfigStore::CommitLocked(State next, std::string* error) {
    if (next.disabled == state_.disabled && next.buttons == state_.buttons) return true;
    if (!Save(next, error)) return false;
    state_ = std::move(next);
    revision_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

bool ConfigStore::SetEnabled(const std::string& modId, bool enabled, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    State next = state_;
    if (enabled) next.disabled.erase(modId);
    else next.disabled.insert(modId);
    return CommitLocked(std::move(next), error);
}

bool ConfigStore::SetButtonScale(int32_t pct, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    State next = state_;
    next.buttons.scalePct = std::clamp(pct, ButtonLayout::kScaleMin, ButtonLayout::kScaleMax);
    return CommitLocked(std::move(next), error);
}

bool ConfigStore::SetButtonOpacity(int32_t pct, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    State next = state_;
    next.buttons.opacityPct = std::clamp(pct, ButtonLayout::kOpacityMin, ButtonLayout::kOpacityMax);
    return CommitLocked(std::move(next), error);
}

bool ConfigStore::SetButtonPosition(const std::string& id, ButtonPlacement p, std::string* error) {
    if (id.empty()) {
        if (error) *error = "botão sem id";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    State next = state_;
    next.buttons.positions[id] = {std::clamp(p.x, 0, ButtonLayout::kPlacementMax),
                                  std::clamp(p.y, 0, ButtonLayout::kPlacementMax)};
    return CommitLocked(std::move(next), error);
}

bool ConfigStore::ResetButtons(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    State next = state_;
    next.buttons = ButtonLayout{};
    return CommitLocked(std::move(next), error);
}

bool ConfigStore::Save(const State& state, std::string* error) const {
    rt::JsonWriter w;
    w.BeginObject();
    w.Key("schema").Int(1);
    w.Key("disabled_mods").BeginArray();
    for (const std::string& id : state.disabled) w.String(id);
    w.EndArray();
    w.Key("buttons").BeginObject();
    w.Key("scale").Int(state.buttons.scalePct);
    w.Key("opacity").Int(state.buttons.opacityPct);
    w.Key("positions").BeginObject();
    for (const auto& [id, p] : state.buttons.positions) w.Key(id).BeginArray().Int(p.x).Int(p.y).EndArray();
    w.EndObject();
    w.EndObject();
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
