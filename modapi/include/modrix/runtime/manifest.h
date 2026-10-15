#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "modrix/modrix_api.h"
#include "modrix/runtime/semver.h"

namespace modrix::rt {

struct Dependency {
    std::string id;
    VersionReq req;
    bool optional = false;  // ausente é ok; se presente, precisa casar e carrega antes
};

// Conteúdo de manifest.json (esquema em modapi/README.md).
struct ModManifest {
    uint32_t apiVersion = 0;       // "modrix_api"
    std::string id;                // "modrix.example_item"
    std::string name;              // nome exibido
    SemVer version;
    std::vector<std::string> authors;
    std::string description;
    std::vector<Dependency> dependencies;

    // "content": o que o mod declara adicionar, por tipo. Permite à interface
    // do Modrix mostrar o conteúdo sem executar código e ao runtime recusar
    // registros não declarados.
    std::array<std::vector<std::string>, MODRIX_CONTENT_KIND_COUNT> content;

    // "provides": canais de dados que o mod expõe à interface.
    std::vector<std::string> provides;

    // "entry": plataforma -> caminho relativo da biblioteca nativa.
    std::map<std::string, std::string> entries;

    std::filesystem::path dir;  // pasta do mod (preenchido ao carregar do disco)

    bool Declares(ModrixContentKind kind, const std::string& name) const;
};

// Erros vêm com o campo afetado ("dependencies.foo: faixa inválida").
std::optional<ModManifest> ParseManifest(const std::string& json, std::string* error);
std::optional<ModManifest> LoadManifest(const std::filesystem::path& modDir, std::string* error);

// Regras de nomes: id do mod e chaves locais de conteúdo.
bool IsValidModId(const std::string& id);        // [a-z0-9]+([._-][a-z0-9]+)*, até 64
bool IsValidContentName(const std::string& n);   // [a-z0-9_]{1,64}
// Caminho relativo que não sai da pasta do mod (sem raiz, sem "..").
bool IsSafeRelativePath(const std::string& p);

// Chave de plataforma desta build: "android-arm64", "linux-x86_64", "windows-x86_64"...
const char* CurrentPlatformKey();

// Nome do tipo de conteúdo no manifest: "items", "npcs", ...
const char* ContentKindKey(ModrixContentKind kind);

}  // namespace modrix::rt
