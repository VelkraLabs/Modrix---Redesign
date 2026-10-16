#pragma once

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "tlmod/tlmod_api.h"
#include "tlmod/runtime/content_registry.h"
#include "tlmod/runtime/item_persistence.h"
#include "tlmod/runtime/manifest.h"

namespace tlmod::rt {

class GameAdapter;

enum class ModState {
    Discovered,  // manifest lido
    Rejected,    // manifest inválido, dependência ausente/incompatível, ciclo...
    Loaded,      // init retornou 0
    Failed,      // biblioteca não abriu, símbolo ausente ou init != 0
    Skipped,     // uma dependência obrigatória não carregou
    Disabled,    // desligado pelo usuário (configuração); nem é aberto
};
const char* ToString(ModState s);

struct ModRecord {
    ModManifest manifest;
    ModState state = ModState::Discovered;
    std::string error;
    TLModModInitFn builtinInit = nullptr;  // mods embutidos no runtime (sem biblioteca)
    // De onde a biblioteca foi aberta (a cópia privada, quando há cache). Vazio
    // para mods embutidos ou se nem chegou a abrir.
    std::filesystem::path libraryPath;
};

// Descobre, valida, ordena e carrega mods; dono do ContentRegistry e da
// tabela TLModHostApi entregue aos mods.
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
    void AddBuiltin(ModManifest manifest, TLModModInitFn init);

    // Mods desligados pelo usuário (lidos da configuração antes de LoadAll).
    // Não são abertos; quem depende deles (obrigatoriamente) fica Skipped.
    // Ligar/desligar só vale na próxima abertura do jogo: um mod carregado
    // nunca é descarregado com o jogo rodando.
    void SetDisabled(std::set<std::string> ids) { disabled_ = std::move(ids); }
    const std::set<std::string>& Disabled() const { return disabled_; }

    // Pasta privada para onde a biblioteca de cada mod é copiada antes do
    // dlopen (ver library_cache.h). Vazio = dlopen direto da pasta do mod.
    // Configurar antes de LoadAll.
    void SetLibraryCacheDir(std::filesystem::path dir) { libraryCacheDir_ = std::move(dir); }
    const std::filesystem::path& LibraryCacheDir() const { return libraryCacheDir_; }

    // Valida, resolve dependências, ordena e chama o init de cada mod.
    // Pode ser chamado uma vez. Retorna quantos mods carregaram.
    size_t LoadAll();

    const std::vector<ModRecord>& Mods() const { return mods_; }
    const ModRecord* Find(const std::string& id) const;
    const std::vector<std::string>& LoadOrder() const { return order_; }
    ContentRegistry& Registry() { return registry_; }
    const ContentRegistry& Registry() const { return registry_; }
    const TLModHostApi& HostApi() const;

    // NPC de mod que ocupa o slot de Main.npc (TLMOD_INVALID_ID = original).
    TLModId NpcInSlot(int32_t index) const;

    // Texturas de mod: PNGs lidos no LoadAll, esperando a vez no frame handler
    // (thread principal), e quantas o jogo já criou.
    size_t PendingTextures() const;
    // Itens cuja casca ainda não foi avaliada (o jogo ainda não respondeu).
    size_t PendingShellChecks() const;
    int32_t TexturesCreated() const;

    // Itens de mod fora do arquivo do jogo (arquivo paralelo). Os ganchos de
    // save/load só entram no fim de LoadAll, com todos os IDs já atribuídos;
    // configurar (SetCloudDirectory) antes disso.
    ItemPersistence& Persistence() { return *persistence_; }
    const ItemPersistence& Persistence() const { return *persistence_; }

    // --- Camada de interface ---------------------------------------------------
    // Canais registrados, como "mod_id/channel".
    std::vector<std::string> Providers() const;
    // Chama o provedor e devolve o JSON (já validado). Seguro em qualquer
    // thread; chamadas concorrentes ao mesmo provedor são serializadas.
    std::optional<std::string> Query(const std::string& modId, const std::string& channel,
                                     std::string* error = nullptr) const;
    // Canais de comando registrados, como "mod_id/channel".
    std::vector<std::string> CommandChannels() const;
    // Manda um comando (texto definido pelo mod) e devolve a resposta JSON já
    // validada ("{}" se o mod não respondeu nada). Qualquer thread; chamadas ao
    // mesmo canal são serializadas.
    std::optional<std::string> Command(const std::string& modId, const std::string& channel,
                                       const std::string& command, std::string* error = nullptr) const;
    // Catálogo de nomes de itens e NPCs (montado aos poucos no frame handler).
    bool CatalogReady() const;

    // Resolve dependências e devolve a ordem de carga (ids). Marca como
    // Rejected os mods com dependência ausente/incompatível ou em ciclo.
    // Exposto para testes; LoadAll já chama.
    static std::vector<std::string> ResolveOrder(std::vector<ModRecord>& mods);

    struct Impl;  // detalhes (contextos, dispatch de eventos, bibliotecas abertas)

private:
    void LoadTextures();

    GameAdapter* game_;
    ContentRegistry registry_;
    std::vector<ModRecord> mods_;
    std::vector<std::string> order_;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<ItemPersistence> persistence_;
    bool loaded_ = false;
    std::set<std::string> disabled_;
    std::filesystem::path libraryCacheDir_;
};

}  // namespace tlmod::rt
