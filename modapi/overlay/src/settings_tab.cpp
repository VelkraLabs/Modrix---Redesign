#include "tlmod/overlay/settings_tab.h"

#include <algorithm>

#include "tlmod/overlay/config_store.h"
#include "tlmod/overlay/panel.h"
#include "tlmod/runtime/log.h"

namespace tlmod::overlay {

namespace {

std::string Percent(int32_t pct) { return std::to_string(pct) + "%"; }

SettingsRow Slider(const char* label, int32_t pct, int32_t min, int32_t max) {
    SettingsRow r;
    r.kind = SettingsRow::Kind::Slider;
    r.label = label;
    r.value = Percent(pct);
    r.steps = (max - min) / ModSettingsTab::kStepPct;
    r.step = std::clamp((pct - min + ModSettingsTab::kStepPct / 2) / ModSettingsTab::kStepPct, 0, r.steps);
    return r;
}

void LogSaveError(const std::string& error) {
    rt::Log(TLMOD_LOG_ERROR, "configurações", "não foi possível salvar a configuração: " + error);
}

}  // namespace

std::vector<SettingsRow> ModSettingsTab::Rows() const {
    const ButtonLayout b = config_.Buttons();
    std::vector<SettingsRow> rows(kRowCount);
    rows[kRowPosition].label = "Posição dos botões de mods";
    rows[kRowPosition].value = "Editar";
    rows[kRowScale] = Slider("Tamanho dos botões de mods", b.scalePct, ButtonLayout::kScaleMin, ButtonLayout::kScaleMax);
    rows[kRowOpacity] =
        Slider("Opacidade dos botões de mods", b.opacityPct, ButtonLayout::kOpacityMin, ButtonLayout::kOpacityMax);
    return rows;
}

bool ModSettingsTab::Click(int32_t row) {
    if (row != kRowPosition) return false;
    if (!panel_) {
        rt::Log(TLMOD_LOG_WARN, "configurações", "overlay indisponível: sem modo de edição dos botões");
        return false;
    }
    panel_->RequestLayoutEdit();
    rt::Log(TLMOD_LOG_INFO, "configurações", "modo de edição dos botões de mods");
    return true;
}

void ModSettingsTab::SetStep(int32_t row, int32_t step) {
    std::string error;
    bool ok = true;
    if (row == kRowScale) {
        const int32_t steps = (ButtonLayout::kScaleMax - ButtonLayout::kScaleMin) / kStepPct;
        ok = config_.SetButtonScale(ButtonLayout::kScaleMin + std::clamp(step, 0, steps) * kStepPct, &error);
    } else if (row == kRowOpacity) {
        const int32_t steps = (ButtonLayout::kOpacityMax - ButtonLayout::kOpacityMin) / kStepPct;
        ok = config_.SetButtonOpacity(ButtonLayout::kOpacityMin + std::clamp(step, 0, steps) * kStepPct, &error);
    }
    if (!ok) LogSaveError(error);
}

void ModSettingsTab::Reset() {
    std::string error;
    if (config_.ResetButtons(&error))
        rt::Log(TLMOD_LOG_INFO, "configurações", "botões de mods no tamanho, opacidade e posição padrão");
    else
        LogSaveError(error);
}

}  // namespace tlmod::overlay
