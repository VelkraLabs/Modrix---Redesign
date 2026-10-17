// Configuração do usuário: quais mods estão desligados.
//
// Arquivo JSON (tlmod_config.json, ao lado da pasta mods/):
//   { "schema": 1, "disabled_mods": ["tlmod.example_boss"] }
//
// Ligar/desligar vale na PRÓXIMA abertura do jogo: o runtime lê este arquivo
// antes de ModLoader::LoadAll e nunca descarrega um mod com o jogo rodando.
//
// Thread-safe: o painel (thread de UI) e a Oficina de Mods nativa (thread do
// jogo) mudam a mesma configuração. Revision() aumenta a cada mudança.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>

namespace tlmod::overlay {

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path file) : file_(std::move(file)) {}

    // Arquivo ausente = configuração vazia (tudo ligado), e não é erro.
    // Arquivo corrompido = erro; a configuração fica vazia.
    bool Load(std::string* error);

    bool IsEnabled(const std::string& modId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return disabled_.count(modId) == 0;
    }
    std::set<std::string> Disabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return disabled_;
    }
    uint64_t Revision() const { return revision_.load(std::memory_order_acquire); }

    // Altera e salva na hora (arquivo temporário + rename: nunca deixa o
    // arquivo pela metade). Se salvar falhar, nada muda em memória.
    bool SetEnabled(const std::string& modId, bool enabled, std::string* error);

    const std::filesystem::path& Path() const { return file_; }

private:
    bool Save(const std::set<std::string>& disabled, std::string* error) const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::set<std::string> disabled_;
    std::atomic<uint64_t> revision_{1};
};

}  // namespace tlmod::overlay
