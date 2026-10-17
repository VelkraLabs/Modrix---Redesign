// Configuração do usuário: quais mods estão desligados e onde ficam os botões
// de mods na tela.
//
// Arquivo JSON (tlmod_config.json, ao lado da pasta mods/):
//   { "schema": 1, "disabled_mods": ["tlmod.example_boss"],
//     "buttons": { "scale": 100, "opacity": 100, "positions": { "menu": [600, 1500] } } }
//
// Ligar/desligar vale na PRÓXIMA abertura do jogo: o runtime lê este arquivo
// antes de ModLoader::LoadAll e nunca descarrega um mod com o jogo rodando. Os
// botões mudam na hora (o overlay relê a configuração pela revisão).
//
// Thread-safe: o painel (thread de UI) e as telas nativas (Oficina de Mods e a
// aba Mods das Configurações, thread do jogo) mudam a mesma configuração.
// Revision() aumenta a cada mudança.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace tlmod::overlay {

// Centro de um botão, em décimos de milésimo da largura e da altura da tela
// (0..10000): a mesma posição relativa em qualquer resolução.
struct ButtonPlacement {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const ButtonPlacement& o) const { return x == o.x && y == o.y; }
};

// Botões de mods na tela (o "M" e os ícones das gavetas e modais).
struct ButtonLayout {
    static constexpr int32_t kScaleMin = 50, kScaleMax = 150, kScaleDefault = 100;
    static constexpr int32_t kOpacityMin = 30, kOpacityMax = 100, kOpacityDefault = 100;
    static constexpr int32_t kPlacementMax = 10000;

    int32_t scalePct = kScaleDefault;      // tamanho, em % do padrão
    int32_t opacityPct = kOpacityDefault;  // opacidade fora do modo de edição
    // Só os botões que o usuário moveu; os outros ficam na posição padrão.
    // Chave: "menu" (o "M"), "checklist" ou "mod:<id do mod>".
    std::map<std::string, ButtonPlacement> positions;

    bool operator==(const ButtonLayout& o) const {
        return scalePct == o.scalePct && opacityPct == o.opacityPct && positions == o.positions;
    }
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path file) : file_(std::move(file)) {}

    // Arquivo ausente = configuração vazia (tudo ligado), e não é erro.
    // Arquivo corrompido = erro; a configuração fica vazia. Valores dos botões
    // fora da faixa são ajustados, e os inválidos ignorados (não é erro).
    bool Load(std::string* error);

    bool IsEnabled(const std::string& modId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.disabled.count(modId) == 0;
    }
    std::set<std::string> Disabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.disabled;
    }
    ButtonLayout Buttons() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.buttons;
    }
    uint64_t Revision() const { return revision_.load(std::memory_order_acquire); }

    // Alteram e salvam na hora (arquivo temporário + rename: nunca deixa o
    // arquivo pela metade). Se salvar falhar, nada muda em memória.
    bool SetEnabled(const std::string& modId, bool enabled, std::string* error);
    bool SetButtonScale(int32_t pct, std::string* error);      // ajustado a [kScaleMin, kScaleMax]
    bool SetButtonOpacity(int32_t pct, std::string* error);    // ajustado a [kOpacityMin, kOpacityMax]
    bool SetButtonPosition(const std::string& id, ButtonPlacement p, std::string* error);  // ajustado a 0..10000
    bool ResetButtons(std::string* error);  // tamanho, opacidade e posições padrão

    const std::filesystem::path& Path() const { return file_; }

private:
    struct State {
        std::set<std::string> disabled;
        ButtonLayout buttons;
    };
    // Grava `next` e, se deu certo, passa a usá-lo (com mutex_ travado).
    bool CommitLocked(State next, std::string* error);
    bool Save(const State& state, std::string* error) const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    State state_;
    std::atomic<uint64_t> revision_{1};
};

}  // namespace tlmod::overlay
