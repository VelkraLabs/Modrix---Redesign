// Configuração do usuário: quais mods estão desligados.
//
// Arquivo JSON (modrix_config.json, ao lado da pasta mods/):
//   { "schema": 1, "disabled_mods": ["modrix.example_boss"] }
//
// Ligar/desligar vale na PRÓXIMA abertura do jogo: o runtime lê este arquivo
// antes de ModLoader::LoadAll e nunca descarrega um mod com o jogo rodando.
#pragma once

#include <filesystem>
#include <set>
#include <string>

namespace modrix::overlay {

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path file) : file_(std::move(file)) {}

    // Arquivo ausente = configuração vazia (tudo ligado), e não é erro.
    // Arquivo corrompido = erro; a configuração fica vazia.
    bool Load(std::string* error);

    bool IsEnabled(const std::string& modId) const { return disabled_.count(modId) == 0; }
    const std::set<std::string>& Disabled() const { return disabled_; }

    // Altera e salva na hora (arquivo temporário + rename: nunca deixa o
    // arquivo pela metade). Se salvar falhar, nada muda em memória.
    bool SetEnabled(const std::string& modId, bool enabled, std::string* error);

    const std::filesystem::path& Path() const { return file_; }

private:
    bool Save(const std::set<std::string>& disabled, std::string* error) const;

    std::filesystem::path file_;
    std::set<std::string> disabled_;
};

}  // namespace modrix::overlay
