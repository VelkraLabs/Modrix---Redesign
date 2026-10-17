#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tlmod/runtime/content_registry.h"
#include "tlmod/runtime/item_shell.h"
#include "tlmod/runtime/png.h"
#include "tlmod/tlmod_api.h"

namespace tlmod::rt {

// --- Persistência: tipos usados pelo adaptador e por ItemPersistence --------------

// Contêineres de item que o jogo grava. Os nomes (ToString) vão para o
// arquivo paralelo: não renomear.
enum class ItemContainer : uint8_t {
    Inventory,       // o jogo grava 0..57 (0..49 principal, moedas, munição)
    Armor,           // armadura, acessórios e vaidade
    Dye,
    MiscEquips,      // pet, luz, carrinho, montaria, gancho
    MiscDyes,
    PiggyBank,       // Porquinho
    Safe,            // Cofre
    DefendersForge,  // Forja do Defensor
    VoidVault,       // Cofre do Vazio
    LoadoutArmor,    // equipamento dos loadouts guardados
    LoadoutDye,
    Temporary,       // 0 mouse, 1 sacrifício da Jornada, 2 Guia, 3 Reforja
    Chest,           // baú do mundo
};
const char* ToString(ItemContainer c);
std::optional<ItemContainer> ParseItemContainer(const std::string& name);
bool IsPlayerContainer(ItemContainer c);

// Um slot. `loadout` só vale para Loadout*; baús são identificados pela
// posição (x, y) em tiles, estável entre save e load (o índice em Main.chest
// pode mudar).
struct ItemSlot {
    ItemContainer container = ItemContainer::Inventory;
    int32_t index = 0;
    int32_t loadout = 0;
    int32_t chestX = 0, chestY = 0;

    bool SameContainer(const ItemSlot& o) const {
        return container == o.container && loadout == o.loadout && chestX == o.chestX && chestY == o.chestY;
    }
    bool operator==(const ItemSlot& o) const { return SameContainer(o) && index == o.index; }
};

// O que o jogo grava de um item.
struct ItemStack {
    int32_t type = 0;
    int32_t stack = 0;
    uint8_t prefix = 0;
    bool favorited = false;
    bool IsAir() const { return type == 0 || stack <= 0; }
};

// O arquivo que o jogo está gravando ou acabou de ler.
struct SaveFile {
    enum class Kind { Player, World };
    Kind kind = Kind::Player;
    std::string path;    // .plr / .wld, como o jogo o chama
    bool cloud = false;  // save na nuvem: o caminho pode não ser um arquivo local
};

// Os slots que o jogo grava para um arquivo: os de um personagem, ou os baús
// de um mundo. Válido só durante o gancho que o recebe.
class ItemSlots {
public:
    virtual ~ItemSlots() = default;
    // Cada slot não vazio.
    virtual void ForEach(const std::function<void(const ItemSlot&, const ItemStack&)>& fn) const = 0;
    // nullopt = o slot não existe (índice fora da faixa, baú que não existe mais...).
    virtual std::optional<ItemStack> Read(const ItemSlot& slot) const = 0;
    // Grava só o que o jogo salva (tipo, pilha, prefixo, favorito).
    virtual bool Write(const ItemSlot& slot, const ItemStack& item) = 0;
    // Quantos slots tem o contêiner de `slot` (mesmo loadout/baú); 0 se não existe.
    virtual int32_t Count(const ItemSlot& slot) const = 0;
};

// Tudo o que o runtime precisa do jogo. O runtime e a API de mods dependem só
// desta interface; o adaptador concreto (ponte IL2CPP no Android, um falso nos
// testes, futuramente outra plataforma) fica atrás dela.
class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    virtual const char* Name() const = 0;

    // Quantidade de IDs originais do tipo, se conhecida. O registro usa isso
    // para garantir que a faixa dos mods começa acima de tudo que é original.
    virtual std::optional<int32_t> VanillaCount(TLModContentKind kind) const = 0;

    // Um único handler: o runtime faz a distribuição para os mods.
    using HurtHandler = std::function<bool(TLModPlayerHurt& ev, bool after)>;
    virtual void SetPlayerHurtHandler(HurtHandler handler) = 0;

    virtual TLModPlayer LocalPlayer() const = 0;
    virtual int32_t PlayerLife(TLModPlayer player) const = 0;  // -1 se inválido

    // --- NPCs -----------------------------------------------------------------
    // Ganchos que o adaptador liga aos pontos do jogo (no Terraria: hooks em
    // NPC.NewNPC, NPC.SetDefaults e NPC.AI). Todos rodam na thread do jogo.
    struct NpcHooks {
        // Antes de criar um NPC: tipo pedido -> tipo que o jogo deve criar.
        std::function<int32_t(int32_t requestedType)> remapSpawnType;
        // Depois de criar. index fora de [0, MaxNpcs()) = o jogo não achou slot.
        std::function<void(int32_t index, int32_t requestedType, int32_t spawnedType)> onSpawned;
        // O slot `index` vai ser reinicializado como `newType` (spawn, transformação, rede).
        std::function<void(int32_t index, int32_t newType)> onSetDefaults;
        // Uma vez por NPC ativo por tick. true = pular a AI original.
        std::function<bool(int32_t index)> onAi;
    };
    virtual void SetNpcHooks(NpcHooks hooks) = 0;

    virtual int32_t MaxNpcs() const = 0;
    // Lê/escreve o estado do NPC no slot. ReadNpc preenche type e shell_type
    // com o tipo que o jogo vê; local[] não é do jogo (o runtime cuida).
    virtual bool ReadNpc(int32_t index, TLModNpcState& out) const = 0;
    virtual bool WriteNpc(int32_t index, const TLModNpcState& in) = 0;
    // Cria um NPC pelo caminho normal do jogo (passa pelos ganchos). -1 se falhar.
    virtual int32_t SpawnNpc(int32_t type, float x, float y) = 0;
    // Centro do jogador local em pixels do mundo. nullopt = sem jogador local,
    // inativo ou morto. Só na thread do jogo.
    virtual std::optional<std::pair<float, float>> LocalPlayerCenter() const = 0;

    // --- Ferramentas de teste (mod tlmod.devtools). Só na thread do jogo. ---
    // Nome de item/NPC original: no idioma do jogo ou o interno. nullopt = o
    // jogo ainda não pode responder (tentar num próximo frame); "" = sem nome.
    virtual std::optional<std::string> ContentName(TLModContentKind kind, int32_t id, bool internalName) = 0;
    // (dia?, ticks) de Main.dayTime/Main.time; nullopt sem mundo.
    virtual std::optional<std::pair<bool, double>> WorldTime() const = 0;
    virtual bool SetWorldTime(bool day, double time, bool jump) = 0;
    virtual bool RestoreLocalPlayer() = 0;  // vida e mana cheias
    // NPCs hostis mortos a até `radius` px de (x, y); ver tlmod_api.h.
    virtual int32_t KillHostileNpcs(float x, float y, float radius) = 0;

    // --- Estado do mundo -----------------------------------------------------
    // Precisam ser seguros em QUALQUER thread (a UI consulta fora da thread do
    // jogo). nullopt = desconhecido.
    virtual std::optional<bool> InWorld() const = 0;
    virtual std::optional<bool> Progress(TLModProgress flag) const = 0;

    // --- Itens de mod --------------------------------------------------------------
    // Um item de mod roda numa casca: o jogo monta o item original
    // def.shellType (é o que todas as tabelas por tipo veem) e o adaptador
    // aplica por cima atributos, nome e tooltip da definição. O ID de mod fica
    // no item como identidade (no Terraria: netID); ItemSlots::Read o devolve.
    struct ItemHooks {
        // Antes de Item.SetDefaults (qualquer thread): tipo pedido -> tipo que o jogo monta.
        std::function<int32_t(int32_t requestedType)> remapType;
        // Definição do item de mod `type`; nulo se não for de mod. Vale enquanto o runtime existir.
        std::function<const ItemDefinition*(int32_t type)> definition;
    };
    virtual void SetItemHooks(ItemHooks hooks) = 0;
    // Põe até `stack` unidades de `type` no inventário do jogador local, pelo
    // SetDefaults do jogo (passa pelos ganchos). Empilha com o que já há e
    // depois usa slots livres do inventário principal. Só na thread do jogo.
    // Devolve quantas unidades entraram.
    virtual int32_t GiveItem(int32_t type, int32_t stack) = 0;
    // Põe `stack` unidades de `type` no chão, sobre o jogador local (o que não
    // coube no inventário): pilhas de até maxStack, montadas como no GiveItem.
    // Ele pega de volta quando abrir espaço. Só na thread do jogo. Devolve
    // quantas unidades foram para o chão.
    virtual int32_t DropItemNearPlayer(int32_t type, int32_t stack) = 0;
    // O que um item original `type` passa para quem o usa como casca (ItemID.Sets
    // e o SetDefaults dele). nullopt = o jogo ainda não pode responder (no
    // Terraria: fora da thread do jogo ou antes do primeiro frame); o runtime
    // tenta de novo no frame handler.
    virtual std::optional<ItemTraits> ItemShellTraits(int32_t type) = 0;

    // --- Receitas de mod -------------------------------------------------------
    // Uma vez, no fim do LoadAll (qualquer thread): as receitas que os mods
    // registraram. O jogo as acrescenta depois das originais, quando puder
    // (no Terraria: depois do Recipe.SetupRecipes, na thread do jogo).
    struct RecipeSpec {
        std::string modId;
        std::string label;  // "mod_id: nome do resultado", para o log
        int32_t result = 0;
        int32_t resultStack = 1;
        std::vector<std::pair<int32_t, int32_t>> ingredients;  // {item, pilha}
        std::vector<int32_t> stations;                         // TileID
        bool needWater = false, needLava = false, needHoney = false;
    };
    virtual void SetRecipes(std::vector<RecipeSpec> recipes) = 0;
    // Uma vez por frame, na thread do jogo (menu e mundo). Nulo desliga.
    // No Terraria mobile é a thread principal do Unity (Main.DoUpdate): a única
    // em que a API do Unity (CreateTexture) pode ser chamada.
    virtual void SetFrameHandler(std::function<void()> handler) = 0;

    // --- Texturas de mod -----------------------------------------------------------
    // Cria a textura no motor a partir de RGBA8 (linha 0 = topo). SÓ dentro do
    // frame handler. Devolve o id (> 0), 0 se falhou, -1 se o jogo ainda não está
    // pronto (tentar num próximo frame).
    virtual int32_t CreateTexture(const Image& image, const std::string& name) = 0;
    struct NpcDraw {
        int32_t texture = 0;     // id de CreateTexture
        int32_t frameCount = 1;  // quadros na folha (vertical)
        int32_t frame = 0;       // quadro a desenhar
    };
    // Na hora de desenhar (thread principal): qual textura usar.
    struct DrawHooks {
        // Item com ID de mod (netID): id da textura, 0 = a da casca.
        std::function<int32_t(int32_t itemType)> itemTexture;
        // NPC no slot `index`: true + out = a textura do mod.
        std::function<bool(int32_t npcIndex, NpcDraw& out)> npcTexture;
        // Nome exibido do NPC no slot `index` (nulo = o da casca). O ponteiro
        // vale enquanto o runtime existir. Qualquer thread.
        std::function<const std::string*(int32_t npcIndex)> npcName;
    };
    virtual void SetDrawHooks(DrawHooks hooks) = 0;

    // --- Save/load ---------------------------------------------------------------
    // Ganchos em volta do save e do load de personagem e mundo (no Terraria:
    // Player.InternalSavePlayerFile, Player.LoadPlayer, WorldFile.InternalSaveWorld,
    // WorldFile.LoadWorld). Rodam na thread que salva/carrega, que pode não ser
    // a do jogo. beforeSave e afterSave de um save vêm na mesma thread, e o
    // afterSave vem sempre, inclusive se o save falhou (ok = false).
    struct PersistenceHooks {
        std::function<void(const SaveFile& file, ItemSlots& slots)> beforeSave;
        std::function<void(const SaveFile& file, ItemSlots& slots, bool ok)> afterSave;
        // Só depois de um load que o jogo conseguiu ler.
        std::function<void(const SaveFile& file, ItemSlots& slots)> afterLoad;
    };
    virtual void SetPersistenceHooks(PersistenceHooks hooks) = 0;
};

}  // namespace tlmod::rt
