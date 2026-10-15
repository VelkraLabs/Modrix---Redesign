#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "modrix/modrix_api.h"
#include "modrix/runtime/content_registry.h"
#include "modrix/runtime/manifest.h"

namespace modrix::rt {

class GameAdapter;

enum class ModState {
    Discovered,  // manifest lido
    Rejected,    // manifest inválido, dependência ausente/incompatível, ciclo...
    Loaded,      // init retornou 0
    Failed,      // biblioteca não abriu, símbolo ausente ou init != 0
    Skipped,     // uma dependência obrigatória não carregou
};
const char* ToString(ModState s);

struct ModRecord {
    ModManifest manifest;
    ModState state = ModState::Discovered;
    std::string error;
    ModrixModInitFn builtinInit = nullptr;  // mods embutidos no runtime (sem biblioteca)
};

// Descobre, valida, ordena e carrega mods; dono do ContentRegistry e da
// tabela ModrixHostApi entregue aos mods.
class ModLoader {
public:
    explicit ModLoader(GameAdapter* game);
    ~ModLoader();
    ModLoader(const ModLoader&) = delete;
    ModLoader& operator=(const ModLoader&) = delete;

    // Lê <dir>/*/manifest.json. Pastas sem manifest são ignoradas; manifests
    // inválidos viram registros Rejected (aparecem no relatório).
    void DiscoverDirectory(const std::filesystem::path& modsDir);

    // Mod compilado junto com o runtime (sem biblioteca externa).
    void AddBuiltin(ModManifest manifest, ModrixModInitFn init);

    // Valida, resolve dependências, ordena e chama o init de cada mod.
    // Pode ser chamado uma vez. Retorna quantos mods carregaram.
    size_t LoadAll();

    const std::vector<ModRecord>& Mods() const { return mods_; }
    const ModRecord* Find(const std::string& id) const;
    const std::vector<std::string>& LoadOrder() const { return order_; }
    ContentRegistry& Registry() { return registry_; }
    const ContentRegistry& Registry() const { return registry_; }
    const ModrixHostApi& HostApi() const;

    // Resolve dependências e devolve a ordem de carga (ids). Marca como
    // Rejected os mods com dependência ausente/incompatível ou em ciclo.
    // Exposto para testes; LoadAll já chama.
    static std::vector<std::string> ResolveOrder(std::vector<ModRecord>& mods);

    struct Impl;  // detalhes (contextos, dispatch de eventos, bibliotecas abertas)

private:
    GameAdapter* game_;
    ContentRegistry registry_;
    std::vector<ModRecord> mods_;
    std::vector<std::string> order_;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

}  // namespace modrix::rt
