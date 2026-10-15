#pragma once

#include <filesystem>
#include <map>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "tlmod/runtime/game_adapter.h"

namespace tlmod::rt {

class ContentRegistry;

// Item de mod guardado fora do arquivo do jogo, pela chave estável.
struct SavedModItem {
    std::string key;  // "mod_id:nome"
    ItemSlot slot;    // onde estava quando o jogo salvou
    int32_t stack = 1;
    uint8_t prefix = 0;
    bool favorited = false;
    // Vazio: o item estava no jogo quando o arquivo paralelo foi gravado.
    // Senão, ele só existe no arquivo paralelo, e o motivo é:
    //   "missing_mod"  o mod não está instalado (a chave não resolve)
    //   "no_space"     o slot e as alternativas estavam ocupados
    std::string orphan;
};

// Regra: nenhum ID de mod chega ao arquivo do jogo.
//
//   antes do save   cada item fora da faixa original sai do slot (vira ar) e é
//                   anotado com a chave "mod_id:nome", pilha, prefixo e posição
//   depois do save  os itens voltam para o lugar e a lista vai para o arquivo
//                   paralelo (<arquivo do jogo> + kSidecarSuffix)
//   depois do load  o arquivo paralelo é lido, cada chave vira o ID atual e o
//                   item volta para o slot (ou para um slot livre equivalente)
//
// Itens cuja chave não resolve (mod removido) ficam no arquivo paralelo como
// órfãos e são regravados a cada save até o mod voltar. Nada é descartado:
// se não houver onde pôr um item, ele também fica guardado como órfão.
//
// Thread-safe: saves de personagem e de mundo podem rodar em threads diferentes.
// Dois saves do mesmo tipo (dois personagens, ou o mesmo duas vezes) rodam um
// de cada vez, do "antes" ao "depois": devolver os itens de um enquanto o
// outro ainda grava poria o ID de mod no arquivo (o mouse, por exemplo, é
// global e entra no save de qualquer personagem).
class ItemPersistence {
public:
    static constexpr const char* kSidecarSuffix = ".tlmod.json";
    static constexpr int kFormatVersion = 1;

    explicit ItemPersistence(const ContentRegistry& registry);

    // Saves na nuvem: o arquivo paralelo vai para <dir>/player|world/<nome>
    // (o caminho do jogo pode não ser local). Vazio: ao lado do caminho.
    // Configuração: chamar antes de os ganchos estarem ativos (ModLoader::LoadAll).
    void SetCloudDirectory(std::filesystem::path dir);
    std::filesystem::path SidecarPath(const SaveFile& file) const;

    // Ganchos (GameAdapter::PersistenceHooks).
    void BeforeSave(const SaveFile& file, ItemSlots& slots);
    void AfterSave(const SaveFile& file, ItemSlots& slots, bool ok);
    void AfterLoad(const SaveFile& file, ItemSlots& slots);

    // Itens deste arquivo que estão só no arquivo paralelo (órfãos).
    std::vector<SavedModItem> Orphans(const std::string& path) const;

    // Formato do arquivo paralelo. ReadSidecar: nullopt + erro se inválido;
    // arquivo ausente = lista vazia.
    static std::optional<std::vector<SavedModItem>> ReadSidecar(const std::filesystem::path& file,
                                                                std::string* error);
    static bool WriteSidecar(const std::filesystem::path& file, SaveFile::Kind kind,
                             const std::vector<SavedModItem>& items, std::string* error);

private:
    struct Stripped {
        SavedModItem entry;  // entry.key vazio: ID fora da faixa original sem dono
        ItemStack original;
    };
    struct InFlight {
        std::vector<Stripped> stripped;
        bool hasTurn = false;  // segura turn_[kind] até o AfterSave
    };
    // A vez de salvar, por tipo de arquivo. Não é um mutex porque é pega no
    // BeforeSave e devolvida no AfterSave, com espera máxima.
    struct Turn {
        std::mutex m;
        std::condition_variable cv;
        bool busy = false;
        bool Take(std::chrono::milliseconds wait);
        void Give();
    };
    // Um save em andamento (antes -> depois), por arquivo e thread.
    struct SaveKey {
        std::string path;
        std::thread::id thread;
        bool operator<(const SaveKey& o) const {
            return path != o.path ? path < o.path : thread < o.thread;
        }
    };

    bool IsVanilla(int32_t type) const;
    // Coloca o item no slot pedido ou num slot livre equivalente; devolve onde.
    std::optional<ItemSlot> Place(ItemSlots& slots, const ItemSlot& wanted, const ItemStack& item) const;
    // Órfãos do arquivo; na primeira vez, carregados do disco (o load pode ter
    // acontecido antes de os ganchos existirem, ou nunca).
    std::vector<SavedModItem>& OrphansLocked(const SaveFile& file);
    std::vector<SavedModItem> ReadOrQuarantine(const SaveFile& file);

    const ContentRegistry& registry_;
    std::filesystem::path cloudDir_;  // só muda antes dos ganchos: sem lock
    mutable std::mutex mutex_;        // inFlight_ e orphans_
    std::map<SaveKey, InFlight> inFlight_;
    Turn turn_[2];  // por SaveFile::Kind
    std::map<std::string, std::vector<SavedModItem>> orphans_;  // caminho do jogo -> órfãos
};

}  // namespace tlmod::rt
