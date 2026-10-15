#include "bridge_adapter.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <utility>

#include "bridge.h"            // il2cpp_bridge/include
#include "terraria_offsets.h"  // kMaxNPCs
#include "tlmod/runtime/log.h"

namespace tlmod::rt {
namespace {

// Handler atual. Trocado por ponteiro atômico porque o hook de Player.Hurt
// pode estar rodando na thread do jogo enquanto o runtime instala o handler.
// Handlers antigos não são liberados (são poucos e evitam use-after-free).
std::atomic<GameAdapter::HurtHandler*> g_handler{nullptr};

bool OnBridgeHurt(TLModHurtEvent* ev, bool after) {
    GameAdapter::HurtHandler* h = g_handler.load(std::memory_order_acquire);
    if (!h || !*h) return true;

    TLModPlayerHurt mev{};
    mev.player = TLMod_GetPlayerIndex(ev->player);
    mev.damage = ev->damage;
    mev.crit = ev->crit;
    mev.pvp = ev->pvp;
    mev.life_before = ev->lifeBefore;
    mev.life_after = ev->lifeAfter;

    bool keep = (*h)(mev, after);
    if (!after) ev->damage = mev.damage;  // mods podem alterar o dano na fase pré
    return keep;
}

// Ganchos de NPC: mesma publicação atômica do handler de Hurt.
std::atomic<GameAdapter::NpcHooks*> g_npcHooks{nullptr};

int32_t NpcRemap(int32_t type) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    return (h && h->remapSpawnType) ? h->remapSpawnType(type) : type;
}
void NpcSpawned(Il2CppObject*, int32_t index, int32_t requested, int32_t spawned) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    if (h && h->onSpawned) h->onSpawned(index, requested, spawned);
}
void NpcSetDefaults(Il2CppObject*, int32_t index, int32_t type) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    if (h && h->onSetDefaults) h->onSetDefaults(index, type);
}
bool NpcAi(Il2CppObject*, int32_t index) {
    GameAdapter::NpcHooks* h = g_npcHooks.load(std::memory_order_acquire);
    return h && h->onAi && index >= 0 && h->onAi(index);
}

// --- Estado do mundo -------------------------------------------------------------
// TLModProgress -> campo estático do jogo (nomes conferidos no dump.cs).
// Mapeamento das mecânicas (MechBoss1..3) segue o código do PC 1.4.4.
struct ProgressField {
    TLModProgress flag;
    const char* klass;
    const char* field;
};
constexpr ProgressField kProgressFields[] = {
    {TLMOD_PROGRESS_HARDMODE, "Main", "hardMode"},  // static_fields+0x360
    {TLMOD_DOWNED_KING_SLIME, "NPC", "downedSlimeKing"},
    {TLMOD_DOWNED_EYE_OF_CTHULHU, "NPC", "downedBoss1"},
    {TLMOD_DOWNED_EVIL_BOSS, "NPC", "downedBoss2"},
    {TLMOD_DOWNED_QUEEN_BEE, "NPC", "downedQueenBee"},
    {TLMOD_DOWNED_SKELETRON, "NPC", "downedBoss3"},
    {TLMOD_DOWNED_DEERCLOPS, "NPC", "downedDeerclops"},
    {TLMOD_DOWNED_QUEEN_SLIME, "NPC", "downedQueenSlime"},
    {TLMOD_DOWNED_DESTROYER, "NPC", "downedMechBoss1"},
    {TLMOD_DOWNED_TWINS, "NPC", "downedMechBoss2"},
    {TLMOD_DOWNED_SKELETRON_PRIME, "NPC", "downedMechBoss3"},
    {TLMOD_DOWNED_PLANTERA, "NPC", "downedPlantBoss"},
    {TLMOD_DOWNED_GOLEM, "NPC", "downedGolemBoss"},
    {TLMOD_DOWNED_DUKE_FISHRON, "NPC", "downedFishron"},
    {TLMOD_DOWNED_EMPRESS_OF_LIGHT, "NPC", "downedEmpressOfLight"},
    {TLMOD_DOWNED_LUNATIC_CULTIST, "NPC", "downedAncientCultist"},
    {TLMOD_DOWNED_MOON_LORD, "NPC", "downedMoonlord"},
    {TLMOD_DOWNED_GOBLIN_ARMY, "NPC", "downedGoblins"},
    {TLMOD_DOWNED_FROST_LEGION, "NPC", "downedFrost"},
    {TLMOD_DOWNED_PIRATES, "NPC", "downedPirates"},
    {TLMOD_DOWNED_MARTIANS, "NPC", "downedMartians"},
    {TLMOD_DOWNED_MOURNING_WOOD, "NPC", "downedHalloweenTree"},
    {TLMOD_DOWNED_PUMPKING, "NPC", "downedHalloweenKing"},
    {TLMOD_DOWNED_EVERSCREAM, "NPC", "downedChristmasTree"},
    {TLMOD_DOWNED_SANTA_NK1, "NPC", "downedChristmasSantank"},
    {TLMOD_DOWNED_ICE_QUEEN, "NPC", "downedChristmasIceQueen"},
    {TLMOD_DOWNED_SOLAR_PILLAR, "NPC", "downedTowerSolar"},
    {TLMOD_DOWNED_VORTEX_PILLAR, "NPC", "downedTowerVortex"},
    {TLMOD_DOWNED_NEBULA_PILLAR, "NPC", "downedTowerNebula"},
    {TLMOD_DOWNED_STARDUST_PILLAR, "NPC", "downedTowerStardust"},
};
static_assert(sizeof(kProgressFields) / sizeof(kProgressFields[0]) == TLMOD_PROGRESS_COUNT,
              "toda flag de TLModProgress precisa de um campo");
static_assert(TLMOD_PROGRESS_COUNT <= 31, "a fotografia usa 31 bits de valor + 31 de 'conhecido'");

// Fotografia num único uint64_t, para leitura consistente de qualquer thread:
//   bits  0..30  valor da flag i
//   bit     31   está num mundo
//   bits 32..62  flag i conhecida (campo resolvido e lido)
//   bit     63   "está num mundo" conhecido (já houve um frame com Instance)
constexpr int kKnownShift = 32;
constexpr uint64_t kInWorldBit = 1ull << 31;
constexpr uint64_t kInWorldKnownBit = 1ull << 63;
std::atomic<uint64_t> g_snapshot{0};

// --- Itens de mod ----------------------------------------------------------------------
// Mesma publicação atômica dos outros ganchos; os antigos não são liberados.
std::atomic<GameAdapter::ItemHooks*> g_itemHooks{nullptr};
std::atomic<std::function<void()>*> g_frameHandler{nullptr};

int32_t RemapItem(int32_t type) {
    GameAdapter::ItemHooks* h = g_itemHooks.load(std::memory_order_acquire);
    return h && h->remapType ? h->remapType(type) : type;
}
const ItemDefinition* ItemDef(int32_t type) {
    GameAdapter::ItemHooks* h = g_itemHooks.load(std::memory_order_acquire);
    return h && h->definition ? h->definition(type) : nullptr;
}

// Por cima da casca: atributos, nome e tooltip da definição. Uma arma
// (damage > 0) numa casca que não se usa vira arma de golpe corpo a corpo.
void ApplyDefinition(Il2CppObject* item, const ItemDefinition& d) {
    TLModItemStats st{};
    TLMod_ReadItemStats(item, &st);
    st.width = d.width;
    st.height = d.height;
    st.maxStack = d.maxStack;
    st.value = d.value;
    st.rare = d.rarity;
    st.damage = d.damage;
    if (d.useTime > 0) st.useTime = st.useAnimation = d.useTime;
    if (d.damage > 0 && st.useStyle == 0) {
        st.useStyle = 1;
        st.melee = true;
    }
    TLMod_WriteItemStats(item, &st);
    TLMod_SetItemName(item, d.displayName.c_str());
    TLMod_SetItemTooltip(item, d.tooltip.c_str());
}

// Callbacks da ponte: rodam dentro de hooks do jogo, em qualquer thread.
// Nenhuma exceção pode sair daqui.
int32_t OnItemRemap(int32_t type) {
    try {
        return RemapItem(type);
    } catch (...) {
        return type;
    }
}
void OnItemDefaults(Il2CppObject* item, int32_t requested, int32_t) {
    try {
        if (const ItemDefinition* d = ItemDef(requested)) ApplyDefinition(item, *d);
    } catch (...) {
    }
}
void OnItemRebuildTooltip(Il2CppObject* item, int32_t netId) {
    try {
        const ItemDefinition* d = ItemDef(netId);
        TLMod_SetItemTooltip(item, d ? d->tooltip.c_str() : nullptr);
    } catch (...) {
    }
}

// Só na thread do jogo (dentro de Main.DoUpdate).
// --- Desenho: textura de mod por item/NPC ------------------------------------------
// Chamados pelos hooks de desenho da ponte (thread principal), um por item ou
// NPC de mod desenhado. Nada pode lançar para dentro do jogo.
std::atomic<GameAdapter::DrawHooks*> g_drawHooks{nullptr};

int32_t OnItemTexture(Il2CppObject*, int32_t netId) {
    GameAdapter::DrawHooks* h = g_drawHooks.load(std::memory_order_acquire);
    try {
        return h && h->itemTexture ? h->itemTexture(netId) : 0;
    } catch (...) {
        return 0;
    }
}
bool OnNpcTexture(Il2CppObject*, int32_t index, TLModNpcDraw* out) {
    GameAdapter::DrawHooks* h = g_drawHooks.load(std::memory_order_acquire);
    if (!h || !h->npcTexture || !out) return false;
    try {
        GameAdapter::NpcDraw d;
        if (!h->npcTexture(index, d)) return false;
        *out = TLModNpcDraw{d.texture, d.frameCount, d.frame};
        return true;
    } catch (...) {
        return false;
    }
}

void OnFrame() {
    static TLModStaticField* fields[TLMOD_PROGRESS_COUNT] = {};
    static bool resolved = false;
    if (!resolved) {  // primeira vez: resolve os 30 campos por nome
        for (const ProgressField& f : kProgressFields)
            fields[f.flag] = TLMod_FindStaticField("Terraria", f.klass, f.field);
        resolved = true;
    }
    uint64_t snap = 0;
    int32_t menu = TLMod_IsGameMenu();
    if (menu >= 0) snap |= kInWorldKnownBit | (menu == 0 ? kInWorldBit : 0);
    for (int i = 0; i < TLMOD_PROGRESS_COUNT; ++i) {
        bool v = false;
        if (TLMod_ReadStaticBool(fields[i], &v))
            snap |= (1ull << (kKnownShift + i)) | (v ? 1ull << i : 0);
    }
    g_snapshot.store(snap, std::memory_order_release);

    if (std::function<void()>* f = g_frameHandler.load(std::memory_order_acquire)) {
        try {
            if (*f) (*f)();
        } catch (...) {
        }
    }
}

// --- Save/load -----------------------------------------------------------------------
std::atomic<GameAdapter::PersistenceHooks*> g_persistence{nullptr};

// Contêiner do runtime -> da ponte. Temporary: no save, o que o jogo grava
// (Main.mouseItem etc., HELD); no load, o que ele leu (_temporaryItemSlots).
TLModItemContainer ToBridge(ItemContainer c, bool saving) {
    switch (c) {
        case ItemContainer::Inventory: return TLMOD_ITEMS_INVENTORY;
        case ItemContainer::Armor: return TLMOD_ITEMS_ARMOR;
        case ItemContainer::Dye: return TLMOD_ITEMS_DYE;
        case ItemContainer::MiscEquips: return TLMOD_ITEMS_MISC_EQUIPS;
        case ItemContainer::MiscDyes: return TLMOD_ITEMS_MISC_DYES;
        case ItemContainer::PiggyBank: return TLMOD_ITEMS_BANK;
        case ItemContainer::Safe: return TLMOD_ITEMS_BANK2;
        case ItemContainer::DefendersForge: return TLMOD_ITEMS_BANK3;
        case ItemContainer::VoidVault: return TLMOD_ITEMS_BANK4;
        case ItemContainer::LoadoutArmor: return TLMOD_ITEMS_LOADOUT_ARMOR;
        case ItemContainer::LoadoutDye: return TLMOD_ITEMS_LOADOUT_DYE;
        case ItemContainer::Temporary: return saving ? TLMOD_ITEMS_HELD : TLMOD_ITEMS_TEMPORARY;
        case ItemContainer::Chest: return TLMOD_ITEMS_CHEST;
    }
    return TLMOD_ITEMS_CONTAINER_COUNT;
}

constexpr ItemContainer kPlayerContainers[] = {
    ItemContainer::Inventory,    ItemContainer::Armor,     ItemContainer::Dye,          ItemContainer::MiscEquips,
    ItemContainer::MiscDyes,     ItemContainer::PiggyBank, ItemContainer::Safe,         ItemContainer::DefendersForge,
    ItemContainer::VoidVault,    ItemContainer::LoadoutArmor, ItemContainer::LoadoutDye, ItemContainer::Temporary,
};

// Slots de um Player (player != nulo) ou dos baús do mundo (Main.chest).
class BridgeItemSlots final : public ItemSlots {
public:
    BridgeItemSlots(Il2CppObject* player, bool saving) : player_(player), saving_(saving) {}

    void ForEach(const std::function<void(const ItemSlot&, const ItemStack&)>& fn) const override {
        auto visit = [&](const ItemSlot& base, TLModItemContainer bc, int32_t group) {
            ItemSlot slot = base;
            int32_t n = TLMod_ItemSlotCount(player_, bc, group);
            for (slot.index = 0; slot.index < n; ++slot.index) {
                std::optional<ItemStack> it = ReadItem(TLMod_ItemSlot(player_, bc, group, slot.index));
                if (it && !it->IsAir()) fn(slot, *it);
            }
        };
        if (!player_) {
            for (int32_t i = 0, n = TLMod_ChestCount(); i < n; ++i) {
                ItemSlot slot;
                slot.container = ItemContainer::Chest;
                if (TLMod_ChestPosition(i, &slot.chestX, &slot.chestY)) visit(slot, TLMOD_ITEMS_CHEST, i);
            }
            return;
        }
        for (ItemContainer c : kPlayerContainers) {
            ItemSlot slot;
            slot.container = c;
            bool loadout = c == ItemContainer::LoadoutArmor || c == ItemContainer::LoadoutDye;
            for (slot.loadout = 0; slot.loadout < (loadout ? 16 : 1); ++slot.loadout) {
                if (TLMod_ItemSlotCount(player_, ToBridge(c, saving_), slot.loadout) < 0) break;
                visit(slot, ToBridge(c, saving_), slot.loadout);
            }
        }
    }
    std::optional<ItemStack> Read(const ItemSlot& slot) const override { return ReadItem(Find(slot)); }
    // Ar: zera só o que o save grava (netID, pilha, prefixo, favorito); o
    // objeto continua montado como a casca. Um item num slot esvaziado assim
    // volta exatamente como estava; em qualquer outro, é montado pelo
    // SetDefaults do jogo (passa pelo hook) e recebe prefixo, pilha e favorito.
    bool Write(const ItemSlot& slot, const ItemStack& v) override {
        Il2CppObject* item = Find(slot);
        TLModItemFields f{};
        if (!item || !TLMod_ReadItem(item, &f)) return false;
        if (v.IsAir()) {
            f.netID = f.stack = 0;
            f.prefix = 0;
            f.favorited = false;
            return TLMod_WriteItem(item, &f);
        }
        const ItemDefinition* def = ItemDef(v.type);
        bool placeholder = f.netID == 0 && f.stack == 0 && f.type != 0 && (!def || f.type == def->shellType);
        if (!placeholder) {
            if (!TLMod_SetItemDefaults(item, v.type)) return false;
            TLMod_ApplyItemPrefix(item, v.prefix);
            if (!TLMod_ReadItem(item, &f) || f.type == 0) return false;  // o jogo não montou (ID sem dono)
        }
        f.netID = v.type;
        f.stack = v.stack;
        f.prefix = v.prefix;
        f.favorited = v.favorited;
        return TLMod_WriteItem(item, &f);
    }
    int32_t Count(const ItemSlot& slot) const override {
        int32_t group = 0;
        if (!Locate(slot, group)) return 0;
        return std::max(0, TLMod_ItemSlotCount(player_, ToBridge(slot.container, saving_), group));
    }

private:
    static std::optional<ItemStack> ReadItem(Il2CppObject* item) {
        TLModItemFields f{};
        if (!TLMod_ReadItem(item, &f)) return std::nullopt;
        // O que vai para o arquivo é netID; type, para o resto do jogo. Um
        // item de mod tem os dois iguais; fica com o maior para nunca deixar
        // passar um ID de mod (variantes antigas têm netID < 0 < type).
        return ItemStack{std::max(f.type, f.netID), f.stack, f.prefix, f.favorited};
    }
    // Contêiner + grupo da ponte para o slot (baú: índice atual pela posição).
    bool Locate(const ItemSlot& slot, int32_t& group) const {
        if (slot.container == ItemContainer::Chest) {
            if (player_) return false;
            group = ChestIndex(slot.chestX, slot.chestY);
            return group >= 0;
        }
        if (!player_) return false;
        group = slot.loadout;
        return true;
    }
    Il2CppObject* Find(const ItemSlot& slot) const {
        int32_t group = 0;
        if (!Locate(slot, group)) return nullptr;
        return TLMod_ItemSlot(player_, ToBridge(slot.container, saving_), group, slot.index);
    }
    // Main.chest pode mudar durante o save (o jogo segue rodando): confere a
    // posição a cada uso e refaz o índice se não bater.
    int32_t ChestIndex(int32_t x, int32_t y) const {
        auto it = chestIndex_.find({x, y});
        int32_t cx = 0, cy = 0;
        if (it != chestIndex_.end() && TLMod_ChestPosition(it->second, &cx, &cy) && cx == x && cy == y)
            return it->second;
        chestIndex_.clear();
        for (int32_t i = 0, n = TLMod_ChestCount(); i < n; ++i)
            if (TLMod_ChestPosition(i, &cx, &cy)) chestIndex_.emplace(std::make_pair(cx, cy), i);
        it = chestIndex_.find({x, y});
        return it == chestIndex_.end() ? -1 : it->second;
    }

    Il2CppObject* player_;
    bool saving_;
    mutable std::map<std::pair<int32_t, int32_t>, int32_t> chestIndex_;
};

// Roda na thread que salva/carrega, dentro de um hook do jogo: nenhuma
// exceção pode sair daqui.
void OnBridgeSave(const TLModSaveEvent* ev, TLModSavePhase phase) {
    GameAdapter::PersistenceHooks* h = g_persistence.load(std::memory_order_acquire);
    if (!h || !ev || !ev->path) return;
    try {
        SaveFile file;
        file.kind = ev->kind == TLMOD_SAVE_PLAYER ? SaveFile::Kind::Player : SaveFile::Kind::World;
        file.path = ev->path;
        file.cloud = ev->cloud;
        Il2CppObject* player = file.kind == SaveFile::Kind::Player ? ev->player : nullptr;
        if (file.kind == SaveFile::Kind::Player && !player) return;
        BridgeItemSlots slots(player, phase != TLMOD_AFTER_LOAD);
        switch (phase) {
            case TLMOD_BEFORE_SAVE:
                if (h->beforeSave) h->beforeSave(file, slots);
                break;
            case TLMOD_AFTER_SAVE:
                if (h->afterSave) h->afterSave(file, slots, ev->ok);
                break;
            case TLMOD_AFTER_LOAD:
                if (ev->ok && h->afterLoad) h->afterLoad(file, slots);
                break;
        }
    } catch (const std::exception& e) {
        Log(TLMOD_LOG_ERROR, "saves", std::string("exceção no gancho de save/load: ") + e.what());
    } catch (...) {
        Log(TLMOD_LOG_ERROR, "saves", "exceção desconhecida no gancho de save/load");
    }
}

}  // namespace

BridgeGameAdapter::BridgeGameAdapter() { TLMod_SetFrameCallback(&OnFrame); }

void BridgeGameAdapter::SetItemHooks(ItemHooks hooks) {
    bool any = hooks.remapType || hooks.definition;
    g_itemHooks.store(any ? new ItemHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    TLModItemCallbacks cb{&OnItemRemap, &OnItemDefaults, &OnItemRebuildTooltip};
    TLMod_SetItemCallbacks(any ? &cb : nullptr);
}

void BridgeGameAdapter::SetFrameHandler(std::function<void()> handler) {
    g_frameHandler.store(handler ? new std::function<void()>(std::move(handler)) : nullptr,
                         std::memory_order_release);
}

// Antigos ficam vazados de propósito: um desenho em andamento pode estar com eles.
void BridgeGameAdapter::SetDrawHooks(DrawHooks hooks) {
    bool any = hooks.itemTexture || hooks.npcTexture;
    g_drawHooks.store(any ? new DrawHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    TLModDrawCallbacks cb{&OnItemTexture, &OnNpcTexture};
    TLMod_SetDrawCallbacks(any ? &cb : nullptr);
}

int32_t BridgeGameAdapter::CreateTexture(const Image& image, const std::string& name) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4)
        return 0;
    return TLMod_CreateTexture(image.rgba.data(), image.width, image.height, name.c_str());
}

int32_t BridgeGameAdapter::GiveItem(int32_t type, int32_t stack) {
    Il2CppObject* player = TLMod_GetLocalPlayer();
    if (!player || stack < 1) return 0;
    const int32_t shell = RemapItem(type);
    const int32_t slots = std::min(TLMod_ItemSlotCount(player, TLMOD_ITEMS_INVENTORY, 0), 50);  // inventário principal
    int32_t left = stack;
    TLModItemFields f{};
    TLModItemStats st{};
    // 1) Empilha com o mesmo item (o critério de Item.IsTheSameAs: netID e type).
    for (int32_t i = 0; i < slots && left > 0; ++i) {
        Il2CppObject* item = TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, i);
        if (!TLMod_ReadItem(item, &f) || f.stack <= 0 || f.netID != type || f.type != shell) continue;
        if (!TLMod_ReadItemStats(item, &st) || f.stack >= st.maxStack) continue;
        int32_t add = std::min(left, st.maxStack - f.stack);
        f.stack += add;
        left -= add;
        TLMod_WriteItem(item, &f);
    }
    // 2) Slots livres: o jogo monta o item (SetDefaults -> hook -> casca + definição).
    for (int32_t i = 0; i < slots && left > 0; ++i) {
        Il2CppObject* item = TLMod_ItemSlot(player, TLMOD_ITEMS_INVENTORY, 0, i);
        if (!TLMod_ReadItem(item, &f) || (f.type != 0 && f.stack > 0)) continue;
        if (!TLMod_SetItemDefaults(item, type) || !TLMod_ReadItem(item, &f) || f.type == 0) break;
        TLMod_ReadItemStats(item, &st);
        f.stack = std::min(left, std::max(1, st.maxStack));
        left -= f.stack;
        TLMod_WriteItem(item, &f);
    }
    return stack - left;
}

void BridgeGameAdapter::SetPersistenceHooks(PersistenceHooks hooks) {
    bool any = hooks.beforeSave || hooks.afterSave || hooks.afterLoad;
    // Os antigos não são liberados: um save pode estar no meio, em outra thread.
    g_persistence.store(any ? new PersistenceHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    TLMod_SetSaveCallback(any ? &OnBridgeSave : nullptr);
}

std::optional<bool> BridgeGameAdapter::InWorld() const {
    uint64_t s = g_snapshot.load(std::memory_order_acquire);
    if (!(s & kInWorldKnownBit)) return std::nullopt;
    return (s & kInWorldBit) != 0;
}

std::optional<bool> BridgeGameAdapter::Progress(TLModProgress flag) const {
    if (flag < 0 || flag >= TLMOD_PROGRESS_COUNT) return std::nullopt;
    uint64_t s = g_snapshot.load(std::memory_order_acquire);
    if (!(s & (1ull << (kKnownShift + flag)))) return std::nullopt;
    return (s & (1ull << flag)) != 0;
}

std::optional<int32_t> BridgeGameAdapter::VanillaCount(TLModContentKind) const {
    // ItemID.Count, NPCID.Count etc. são `const` no C#: o compilador embute o
    // valor e não existe campo para ler em runtime. O registro usa os valores
    // do dump (ContentRegistry::DefaultRanges). WallID.Count é static readonly
    // e poderia ser lido via il2cpp_field_static_get_value numa próxima versão.
    return std::nullopt;
}

void BridgeGameAdapter::SetPlayerHurtHandler(HurtHandler handler) {
    HurtHandler* next = handler ? new HurtHandler(std::move(handler)) : nullptr;
    g_handler.store(next, std::memory_order_release);
    TLMod_SetHurtCallback(next ? &OnBridgeHurt : nullptr);
}

TLModPlayer BridgeGameAdapter::LocalPlayer() const {
    return TLMod_GetPlayerIndex(TLMod_GetLocalPlayer());
}

int32_t BridgeGameAdapter::PlayerLife(TLModPlayer player) const {
    Il2CppObject* p = TLMod_GetPlayerByIndex(player);
    return p ? TLMod_GetStatLife(p) : -1;
}

void BridgeGameAdapter::SetNpcHooks(NpcHooks hooks) {
    bool any = hooks.remapSpawnType || hooks.onSpawned || hooks.onSetDefaults || hooks.onAi;
    g_npcHooks.store(any ? new NpcHooks(std::move(hooks)) : nullptr, std::memory_order_release);
    TLModNpcCallbacks cb{&NpcRemap, &NpcSpawned, &NpcSetDefaults, &NpcAi};
    TLMod_SetNpcCallbacks(any ? &cb : nullptr);
}

int32_t BridgeGameAdapter::MaxNpcs() const { return terraria::kMaxNPCs; }

bool BridgeGameAdapter::ReadNpc(int32_t index, TLModNpcState& out) const {
    TLModNpcFields f{};
    if (!TLMod_ReadNpc(TLMod_GetNpcByIndex(index), &f) || !f.active) return false;
    out.index = index;
    out.type = out.shell_type = f.type;
    out.x = f.x;
    out.y = f.y;
    out.vx = f.vx;
    out.vy = f.vy;
    out.life = f.life;
    out.life_max = f.lifeMax;
    out.damage = f.damage;
    out.defense = f.defense;
    out.target = f.target;
    for (int i = 0; i < 4; ++i) out.ai[i] = f.ai[i];
    return true;
}

bool BridgeGameAdapter::WriteNpc(int32_t index, const TLModNpcState& in) {
    Il2CppObject* npc = TLMod_GetNpcByIndex(index);
    TLModNpcFields f{};
    if (!TLMod_ReadNpc(npc, &f)) return false;
    f.x = in.x;
    f.y = in.y;
    f.vx = in.vx;
    f.vy = in.vy;
    f.life = in.life;
    f.lifeMax = in.life_max;
    f.damage = in.damage;
    f.defense = in.defense;
    f.target = in.target;
    for (int i = 0; i < 4; ++i) f.ai[i] = in.ai[i];
    return TLMod_WriteNpc(npc, &f);
}

int32_t BridgeGameAdapter::SpawnNpc(int32_t type, float x, float y) {
    int32_t index = TLMod_SpawnNpc(static_cast<int32_t>(x), static_cast<int32_t>(y), type);
    return (index >= 0 && index < terraria::kMaxNPCs) ? index : -1;
}

}  // namespace tlmod::rt
