// GameAdapter falso para testes: simula players, o fluxo de Player.Hurt, NPCs
// e o save/load de personagem e mundo.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "tlmod/runtime/game_adapter.h"

class FakeGame final : public tlmod::rt::GameAdapter {
public:
    std::array<std::optional<int32_t>, TLMOD_CONTENT_KIND_COUNT> vanilla{};
    std::array<int32_t, 4> life{400, 300, 200, 100};
    TLModPlayer local = 0;
    HurtHandler handler;

    const char* Name() const override { return "fake"; }
    std::optional<int32_t> VanillaCount(TLModContentKind k) const override { return vanilla[k]; }
    void SetPlayerHurtHandler(HurtHandler h) override { handler = std::move(h); }
    TLModPlayer LocalPlayer() const override { return local; }
    int32_t PlayerLife(TLModPlayer p) const override {
        return (p >= 0 && p < static_cast<int>(life.size())) ? life[static_cast<size_t>(p)] : -1;
    }

    // --- NPCs -------------------------------------------------------------------
    static constexpr int kMaxNpcs = 200;
    static constexpr int32_t kVanillaNpcs = 688;
    struct Npc {
        bool active = false;
        TLModNpcState s{};  // só os campos "do jogo" (local[] não existe aqui)
    };
    std::array<Npc, kMaxNpcs> npcs{};
    NpcHooks npcHooks;
    bool vanillaSetDefaultsOverflow = false;  // um tipo de mod chegou ao SetDefaults original

    void SetNpcHooks(NpcHooks h) override { npcHooks = std::move(h); }
    int32_t MaxNpcs() const override { return kMaxNpcs; }
    bool ReadNpc(int32_t i, TLModNpcState& out) const override {
        if (i < 0 || i >= kMaxNpcs || !npcs[static_cast<size_t>(i)].active) return false;
        out = npcs[static_cast<size_t>(i)].s;
        std::memset(out.local, 0, sizeof(out.local));
        return true;
    }
    bool WriteNpc(int32_t i, const TLModNpcState& in) override {
        if (i < 0 || i >= kMaxNpcs) return false;
        TLModNpcState& s = npcs[static_cast<size_t>(i)].s;
        int32_t type = s.type;  // o jogo não deixa mudar type/index por aqui
        s = in;
        s.type = s.shell_type = type;
        s.index = i;
        return true;
    }
    int32_t SpawnNpc(int32_t type, float x, float y) override { return NewNpc(type, x, y); }

    // NPC.SetDefaults: o jogo de verdade indexa arrays por tipo.
    void SetDefaults(int32_t i, int32_t type) {
        if (npcHooks.onSetDefaults) npcHooks.onSetDefaults(i, type);
        if (type <= 0 || type >= kVanillaNpcs) vanillaSetDefaultsOverflow = true;
        TLModNpcState& s = npcs[static_cast<size_t>(i)].s;
        s = TLModNpcState{};
        s.index = i;
        s.type = s.shell_type = type;
        s.life = s.life_max = 100 + type;
        s.damage = 10;
        s.defense = 5;
    }
    // NPC.NewNPC: remap (pré) -> slot -> SetDefaults -> onSpawned (pós).
    int32_t NewNpc(int32_t type, float x, float y) {
        int32_t spawnType = npcHooks.remapSpawnType ? npcHooks.remapSpawnType(type) : type;
        int32_t index = kMaxNpcs;
        for (int i = 0; i < kMaxNpcs; ++i)
            if (!npcs[static_cast<size_t>(i)].active) { index = i; break; }
        if (index < kMaxNpcs) {
            SetDefaults(index, spawnType);
            npcs[static_cast<size_t>(index)].active = true;
            npcs[static_cast<size_t>(index)].s.x = x;
            npcs[static_cast<size_t>(index)].s.y = y;
        }
        if (npcHooks.onSpawned) npcHooks.onSpawned(index, type, spawnType);
        return index < kMaxNpcs ? index : -1;
    }
    // Main.DoUpdateInWorld -> UpdateNPC(i) -> AI(): AI original conta ticks em ai[0].
    void UpdateNpcs() {
        for (int i = 0; i < kMaxNpcs; ++i) {
            if (!npcs[static_cast<size_t>(i)].active) continue;
            if (npcHooks.onAi && npcHooks.onAi(i)) continue;
            npcs[static_cast<size_t>(i)].s.ai[0] += 1.0f;
        }
    }

    // --- Estado do mundo -----------------------------------------------------------
    std::optional<bool> inWorld;  // nullopt = o jogo ainda não rodou um frame
    std::array<std::optional<bool>, TLMOD_PROGRESS_COUNT> progress{};
    std::optional<bool> InWorld() const override { return inWorld; }
    std::optional<bool> Progress(TLModProgress f) const override {
        return (f >= 0 && f < TLMOD_PROGRESS_COUNT) ? progress[static_cast<size_t>(f)] : std::nullopt;
    }

    // --- Itens ------------------------------------------------------------------------
    // Como no jogo: tabelas indexadas pelo tipo do item com o tamanho original
    // (ItemID.Count = 5456): nomes, tooltips (por netID), pilha máxima, texturas,
    // ItemID.Sets. Ler fora delas lança IndexOutOfRangeException e conta em
    // tableOverflows. Item.SetDefaults troca type >= 5456 por ar.
    static constexpr int32_t kItemCount = 5456;
    using ItemContainer = tlmod::rt::ItemContainer;
    using ItemSlot = tlmod::rt::ItemSlot;
    using ItemStack = tlmod::rt::ItemStack;
    using SaveFile = tlmod::rt::SaveFile;

    struct Item {
        int32_t type = 0, stack = 0;
        uint8_t prefix = 0;
        bool favorited = false;
        int32_t netID = 0;  // o que o save grava; = type, exceto num item de mod (casca)
        int32_t width = 0, height = 0, damage = 0, useTime = 0, useAnimation = 0, maxStack = 0, value = 0, rare = 0;
        uint8_t useStyle = 0;
        bool melee = false;
        std::string nameOverride;  // Item._nameOverride
        std::string tooltip;       // texto do ToolTip
        bool IsAir() const { return type == 0 || stack <= 0; }
        // Item original como o load deixa (netID = type), sem passar pelo SetDefaults.
        static Item Raw(int32_t type, int32_t stack, uint8_t prefix = 0, bool favorited = false) {
            Item it;
            it.type = it.netID = type;
            it.stack = stack;
            it.prefix = prefix;
            it.favorited = favorited;
            it.maxStack = 9999;
            return it;
        }
    };

    std::vector<std::string> itemNames = Table<std::string>("item ");
    std::vector<std::string> itemTooltips = Table<std::string>("tooltip ");
    std::vector<int32_t> itemMaxStack = std::vector<int32_t>(kItemCount, 9999);
    std::vector<int32_t> itemTexture = std::vector<int32_t>(kItemCount, 1);
    std::vector<bool> itemShouldNotBeInInventory = std::vector<bool>(kItemCount, false);  // ItemID.Sets
    std::atomic<int> tableOverflows{0};
    std::atomic<int> lastOverflowIndex{0};

    ItemHooks itemHooks;
    std::function<void()> frameHandler;
    void SetItemHooks(ItemHooks h) override { itemHooks = std::move(h); }
    void SetFrameHandler(std::function<void()> h) override { frameHandler = std::move(h); }
    // Main.DoUpdate: um frame na thread do jogo.
    void Frame() {
        frameThread = std::this_thread::get_id();
        inFrame = true;
        if (frameHandler) frameHandler();
        inFrame = false;
        ++frames;
    }
    int frames = 0;

    // --- Texturas (Unity) ---------------------------------------------------------
    // Como na ponte: só dentro do frame (thread principal); -1 enquanto as
    // texturas do jogo não carregaram. Fora do frame conta em offThreadTextures.
    struct Texture2D {
        std::string name;
        int32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
        int frame = 0;  // em qual Frame() foi criada
    };
    std::vector<Texture2D> textures;  // id = posição + 1
    bool texturesReady = true;        // TextureAssets já carregado?
    int32_t rejectTextureWidth = -1;  // simula o jogo recusando (CreateTexture -> 0)
    std::atomic<int> offThreadTextures{0};
    std::thread::id frameThread;
    bool inFrame = false;
    int32_t CreateTexture(const tlmod::rt::Image& img, const std::string& name) override {
        if (!inFrame || std::this_thread::get_id() != frameThread) {
            ++offThreadTextures;
            return 0;
        }
        if (!texturesReady) return -1;
        if (img.width == rejectTextureWidth) return 0;
        textures.push_back({name, img.width, img.height, img.rgba, frames});
        return static_cast<int32_t>(textures.size());
    }
    DrawHooks drawHooks;
    void SetDrawHooks(DrawHooks h) override { drawHooks = std::move(h); }
    // Textura que o desenho usa: as da casca são o tipo; as de mod, kModTexture + id.
    static constexpr int32_t kModTexture = 1000000;
    // Um NPC desenhado: textura e retângulo do quadro na folha (como NPC.frame).
    struct NpcSprite {
        int32_t texture = 0;
        int32_t frameY = 0, frameHeight = 0;
    };
    static constexpr int32_t kShellNpcFrames = 6, kShellNpcFrameHeight = 40;
    NpcSprite DrawNpc(int32_t i) {
        NpcSprite sp;
        const TLModNpcState& s = npcs[static_cast<size_t>(i)].s;
        NpcDraw d;
        if (drawHooks.npcTexture && drawHooks.npcTexture(i, d) && d.texture > 0 &&
            static_cast<size_t>(d.texture) <= textures.size()) {
            const Texture2D& t = textures[static_cast<size_t>(d.texture) - 1];
            sp.texture = kModTexture + d.texture;
            sp.frameHeight = t.height / d.frameCount;
            sp.frameY = d.frame * sp.frameHeight;
            return sp;
        }
        sp.texture = s.shell_type;  // TextureAssets.Npc[type] e a animação da casca
        sp.frameHeight = kShellNpcFrameHeight;
        sp.frameY = (static_cast<int32_t>(s.ai[0]) / 8 % kShellNpcFrames) * kShellNpcFrameHeight;
        return sp;
    }

    // Item.SetDefaults(type), com os ganchos do adaptador em volta (como o hook da ponte).
    void SetDefaults(Item& it, int32_t requested) {
        int32_t type = itemHooks.remapType ? itemHooks.remapType(requested) : requested;
        it = Item{};  // ResetStats
        it.type = it.netID = type;
        if (type < 0 || type >= kItemCount) it.type = it.netID = 0;  // a guarda do jogo
        if (it.type != 0) {
            it.stack = 1;
            it.maxStack = At(itemMaxStack, it.type);
            it.width = 12;
            it.height = 20;
            if (At(itemShouldNotBeInInventory, it.type)) it.stack = 0;
        }
        RebuildTooltip(it);
        if (type != requested && it.type == type) {  // casca montada: identidade + definição
            it.netID = requested;
            ApplyDefinition(it);
        }
    }
    // O que o adaptador aplica por cima da casca (mesmas regras do BridgeGameAdapter).
    void ApplyDefinition(Item& it) {
        const tlmod::rt::ItemDefinition* d = itemHooks.definition ? itemHooks.definition(it.netID) : nullptr;
        if (!d) return;
        it.width = d->width;
        it.height = d->height;
        it.maxStack = d->maxStack;
        it.value = d->value;
        it.rare = d->rarity;
        it.damage = d->damage;
        if (d->useTime > 0) it.useTime = it.useAnimation = d->useTime;
        if (d->damage > 0 && it.useStyle == 0) it.useStyle = 1, it.melee = true;  // arma de golpe
        it.nameOverride = d->displayName;
        it.tooltip = d->tooltip;
    }
    // Item.RebuildTooltip: Lang.GetTooltip(netID). Com o TLMod, o hook da ponte
    // atende um netID de mod sem tocar na tabela.
    void RebuildTooltip(Item& it) {
        if (it.netID >= kItemCount && (itemHooks.remapType || itemHooks.definition)) {
            const tlmod::rt::ItemDefinition* def = itemHooks.definition ? itemHooks.definition(it.netID) : nullptr;
            it.tooltip = def ? def->tooltip : "";
            return;
        }
        it.tooltip = At(itemTooltips, it.netID);
    }
    // Item.Name: _nameOverride ?? Lang.GetItemNameValue(type).
    std::string Name(const Item& it) { return it.nameOverride.empty() ? At(itemNames, it.type) : it.nameOverride; }
    // TextureAssets.Item[type]: o que o desenho do inventário lê.
    // Com o TLMod, um item de mod (netID na faixa de mods) pode trazer a própria.
    int32_t Texture(const Item& it) {
        if (it.netID >= kItemCount && drawHooks.itemTexture) {
            int32_t id = drawHooks.itemTexture(it.netID);
            if (id > 0) return kModTexture + id;
        }
        return At(itemTexture, it.type);
    }
    // Item.IsTheSameAs: empilhar, depositar, pegar do chão.
    static bool IsTheSameAs(const Item& a, const Item& b) { return a.netID == b.netID && a.type == b.type; }
    // Item.Refresh: netDefaults(netID) mantendo pilha, prefixo e favorito.
    void Refresh(Item& it) {
        Item keep = it;
        SetDefaults(it, keep.netID);
        it.stack = keep.stack;
        it.prefix = keep.prefix;
        it.favorited = keep.favorited;
    }
    // new Item() + SetDefaults(type) (Item.NewItem, loja, receita...).
    Item NewItem(int32_t type, int32_t stack = 1, uint8_t prefix = 0, bool favorited = false) {
        Item it;
        SetDefaults(it, type);
        if (!it.IsAir()) it.stack = stack, it.prefix = prefix, it.favorited = favorited;
        return it;
    }
    // Um frame de interface: textura e nome de tudo que o jogador e os baús têm.
    void DrawAll() {
        auto draw = [&](const Item& it) {
            if (it.IsAir()) return;
            Texture(it);
            Name(it);
        };
        ForEachPlayerItem([&](Item& it) { draw(it); });
        for (const auto& c : chests)
            if (c) for (const Item& it : c->item) draw(it);
    }
    template <typename Fn>
    void ForEachPlayerItem(Fn fn) {
        for (Item& it : player.inventory) fn(it);
        for (Item& it : player.armor) fn(it);
        for (Item& it : player.dye) fn(it);
        for (Item& it : player.miscEquips) fn(it);
        for (Item& it : player.miscDyes) fn(it);
        for (auto& b : player.banks) for (Item& it : b) fn(it);
        for (auto& l : player.loadoutArmor) for (Item& it : l) fn(it);
        for (auto& l : player.loadoutDye) for (Item& it : l) fn(it);
        for (Item& it : player.temporary) fn(it);
        for (Item& it : held) fn(it);
    }

    // Jogador local: empilha com o que já tem (IsTheSameAs) e depois usa os
    // slots livres do inventário principal (0..49).
    int32_t GiveItem(int32_t type, int32_t stack) override {
        Item proto = NewItem(type, 1);
        if (proto.IsAir()) return 0;
        int32_t left = stack;
        for (size_t i = 0; i < 50 && left > 0; ++i) {
            Item& it = player.inventory[i];
            if (it.IsAir() || !IsTheSameAs(it, proto) || it.stack >= it.maxStack) continue;
            int32_t add = std::min(left, it.maxStack - it.stack);
            it.stack += add;
            left -= add;
        }
        for (size_t i = 0; i < 50 && left > 0; ++i) {
            Item& it = player.inventory[i];
            if (!it.IsAir()) continue;
            it = proto;
            it.stack = std::min(left, proto.maxStack);
            left -= it.stack;
        }
        return stack - left;
    }

    // --- Saves ---------------------------------------------------------------------
    // Mesmo layout de item do jogo (Player.InternalSavePlayerFile, WorldFile.SaveChests:
    // netID int32, stack, prefix byte). No load tudo passa por SetDefaults (netDefaults):
    // um ID >= 5456 vira ar e o item some; inventário e bancos conferem antes. SaveChests
    // indexa ItemID.Sets.ItemsThatShouldNotBeInInventory[type]. Os baús voltam do load com
    // índices novos (só os não nulos, em ordem).
    struct Player {
        std::array<Item, 58> inventory{};  // o que o save grava
        std::array<Item, 20> armor{};
        std::array<Item, 10> dye{};
        std::array<Item, 5> miscEquips{}, miscDyes{};
        std::array<std::array<Item, 40>, 4> banks{};  // Porquinho, Cofre, Forja, Cofre do Vazio
        std::array<std::array<Item, 20>, 3> loadoutArmor{};
        std::array<std::array<Item, 10>, 3> loadoutDye{};
        std::array<Item, 4> temporary{};  // o que o load leu da seção temporária
    };
    struct Chest {
        int32_t x = 0, y = 0;
        std::array<Item, 40> item{};
    };

    Player player;
    std::array<Item, 4> held{};  // Main.mouseItem, sacrifício, guideItem, reforgeItem
    std::vector<std::optional<Chest>> chests = std::vector<std::optional<Chest>>(16);  // Main.chest
    PersistenceHooks persistence;
    std::function<void()> duringSave;  // entre o "antes" e a gravação (o jogo segue rodando)
    bool failNextSave = false;         // simula uma exceção (disco cheio) no meio do save
    // Saves de personagem e de mundo podem rodar ao mesmo tempo (threads do
    // ThreadPool): as estatísticas abaixo ficam sob statsMutex.
    std::mutex statsMutex;
    int32_t lastSavedMaxId = 0;        // maior netID que o último save gravou
    int32_t worstSavedId = 0;          // maior netID que qualquer save gravou

    void SetPersistenceHooks(PersistenceHooks h) override { persistence = std::move(h); }

    // Player.SavePlayer -> InternalSavePlayerFile.
    bool SavePlayer(const std::string& path, bool cloud = false) {
        return Save(SaveFile{SaveFile::Kind::Player, path, cloud}, [this](Writer& w) { WritePlayer(w); });
    }
    // Player.LoadPlayer. false = o jogo não conseguiu ler (error diz por quê).
    bool LoadPlayer(const std::string& path, std::string* error = nullptr, bool cloud = false) {
        Player p{};
        if (!Load(path, error, [&](Reader& r) { ReadPlayer(r, p); })) return false;
        player = p;
        AfterLoad(SaveFile{SaveFile::Kind::Player, path, cloud});
        return true;
    }
    bool SaveWorld(const std::string& path) {
        return Save(SaveFile{SaveFile::Kind::World, path, false}, [this](Writer& w) { WriteWorld(w); });
    }
    bool LoadWorld(const std::string& path, std::string* error = nullptr) {
        std::vector<std::optional<Chest>> loaded;
        if (!Load(path, error, [&](Reader& r) { ReadWorld(r, loaded); })) return false;
        chests = std::move(loaded);
        AfterLoad(SaveFile{SaveFile::Kind::World, path, false});
        return true;
    }

    // Slots que o jogo grava, vistos pelo runtime. `saving`: Temporary é o
    // que o save grava (held); no load, o que foi lido (player.temporary).
    class Slots final : public tlmod::rt::ItemSlots {
    public:
        Slots(FakeGame& g, SaveFile::Kind kind, bool saving) : g_(g), kind_(kind), saving_(saving) {}
        void ForEach(const std::function<void(const ItemSlot&, const ItemStack&)>& fn) const override {
            auto visit = [&](ItemSlot s) {
                for (s.index = 0; s.index < Count(s); ++s.index)
                    if (Item* it = Ref(s); it && !it->IsAir()) fn(s, Stack(*it));
            };
            if (kind_ == SaveFile::Kind::World) {
                for (const auto& c : g_.chests)
                    if (c) visit(ItemSlot{ItemContainer::Chest, 0, 0, c->x, c->y});
                return;
            }
            for (int c = 0; c <= static_cast<int>(ItemContainer::Temporary); ++c) {
                auto container = static_cast<ItemContainer>(c);
                bool loadout = container == ItemContainer::LoadoutArmor || container == ItemContainer::LoadoutDye;
                for (int32_t l = 0; l < (loadout ? 3 : 1); ++l) visit(ItemSlot{container, 0, l, 0, 0});
            }
        }
        std::optional<ItemStack> Read(const ItemSlot& s) const override {
            Item* it = Ref(s);
            if (!it) return std::nullopt;
            return Stack(*it);
        }
        // Ar: zera só o que o save grava (o objeto continua montado). Um item num
        // slot esvaziado assim volta como estava; em outro slot, pelo SetDefaults.
        bool Write(const ItemSlot& s, const ItemStack& v) override {
            Item* it = Ref(s);
            if (!it) return false;
            if (v.IsAir()) {
                it->netID = it->stack = 0;
                it->prefix = 0;
                it->favorited = false;
                return true;
            }
            const tlmod::rt::ItemDefinition* def =
                g_.itemHooks.definition ? g_.itemHooks.definition(v.type) : nullptr;
            bool placeholder = it->netID == 0 && it->stack == 0 && it->type != 0 && (!def || it->type == def->shellType);
            if (!placeholder) g_.SetDefaults(*it, v.type);
            it->netID = v.type;
            it->stack = v.stack;
            it->prefix = v.prefix;
            it->favorited = v.favorited;
            return true;
        }
        int32_t Count(const ItemSlot& s) const override {
            ItemSlot probe = s;
            int32_t n = 0;
            for (probe.index = 0; Ref(probe); ++probe.index) ++n;
            return n;
        }

    private:
        // O ID que o save gravaria (netID); max com type para variantes antigas (netID < 0).
        static ItemStack Stack(const Item& it) {
            return ItemStack{std::max(it.type, it.netID), it.stack, it.prefix, it.favorited};
        }
        Item* Ref(const ItemSlot& s) const {
            if (s.index < 0) return nullptr;
            auto i = static_cast<size_t>(s.index);
            auto pick = [i](auto& arr) -> Item* { return i < arr.size() ? &arr[i] : nullptr; };
            auto pickL = [&](auto& arrs) -> Item* {
                return s.loadout >= 0 && static_cast<size_t>(s.loadout) < arrs.size() ? pick(arrs[static_cast<size_t>(s.loadout)])
                                                                                     : nullptr;
            };
            if (kind_ == SaveFile::Kind::World) {
                if (s.container != ItemContainer::Chest) return nullptr;
                for (auto& c : g_.chests)
                    if (c && c->x == s.chestX && c->y == s.chestY) return pick(c->item);
                return nullptr;
            }
            Player& p = g_.player;
            switch (s.container) {
                case ItemContainer::Inventory: return pick(p.inventory);
                case ItemContainer::Armor: return pick(p.armor);
                case ItemContainer::Dye: return pick(p.dye);
                case ItemContainer::MiscEquips: return pick(p.miscEquips);
                case ItemContainer::MiscDyes: return pick(p.miscDyes);
                case ItemContainer::PiggyBank: return pick(p.banks[0]);
                case ItemContainer::Safe: return pick(p.banks[1]);
                case ItemContainer::DefendersForge: return pick(p.banks[2]);
                case ItemContainer::VoidVault: return pick(p.banks[3]);
                case ItemContainer::LoadoutArmor: return pickL(p.loadoutArmor);
                case ItemContainer::LoadoutDye: return pickL(p.loadoutDye);
                case ItemContainer::Temporary: return saving_ ? pick(g_.held) : pick(p.temporary);
                case ItemContainer::Chest: return nullptr;
            }
            return nullptr;
        }
        FakeGame& g_;
        SaveFile::Kind kind_;
        bool saving_;
    };

    // Faz o "jogo" aplicar dano: passa pelos mods (pré), aplica, avisa (pós).
    // Retorna o dano aplicado, ou 0 se algum mod cancelou.
    int32_t Hurt(TLModPlayer p, int32_t damage) {
        auto& hp = life[static_cast<size_t>(p)];
        TLModPlayerHurt ev{p, damage, false, false, hp, 0};
        if (handler && !handler(ev, false)) return 0;
        hp -= ev.damage;
        ev.life_after = hp;
        if (handler) handler(ev, true);
        return ev.damage;
    }

private:
    template <typename T>
    static std::vector<T> Table(const char* prefix) {
        std::vector<T> t(kItemCount);
        for (int32_t i = 0; i < kItemCount; ++i) t[static_cast<size_t>(i)] = prefix + std::to_string(i);
        return t;
    }
    // Acesso a uma tabela do jogo pelo tipo (bounds-checked, como no IL2CPP).
    template <typename T>
    T At(const std::vector<T>& table, int32_t index) {
        if (index < 0 || static_cast<size_t>(index) >= table.size()) {
            ++tableOverflows;
            lastOverflowIndex = index;
            throw std::out_of_range("IndexOutOfRangeException: tabela de itens[" + std::to_string(index) + "]");
        }
        return table[static_cast<size_t>(index)];
    }

    struct Writer {
        std::string out;
        int32_t* maxId;
        void I32(int32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
        void I16(int16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); }
        void U8(uint8_t v) { out.push_back(static_cast<char>(v)); }
        void NetId(const Item& it) {
            if (it.netID > *maxId) *maxId = it.netID;
            I32(it.netID);
        }
    };
    struct Reader {
        std::string data;
        size_t pos = 0;
        template <typename T>
        T Get() {
            if (pos + sizeof(T) > data.size()) throw std::runtime_error("EndOfStreamException");
            T v;
            std::memcpy(&v, data.data() + pos, sizeof(T));
            pos += sizeof(T);
            return v;
        }
    };
    // Item.netDefaults -> SetDefaults (com a guarda type >= 5456 -> ar).
    Item NetDefaults(int32_t id) {
        Item it;
        SetDefaults(it, id);
        return it;
    }
    static void WriteSerialized(Writer& w, const Item& it) {  // Item.Serialize
        w.NetId(it);
        w.I32(it.stack);
        w.U8(it.prefix);
    }
    Item ReadSerialized(Reader& r) {  // Item.DeserializeFrom
        Item it = NetDefaults(r.Get<int32_t>());
        it.stack = r.Get<int32_t>();
        it.prefix = r.Get<uint8_t>();
        return it;
    }

    void WritePlayer(Writer& w) const {
        for (const Item& it : player.armor) w.NetId(it), w.U8(it.prefix);
        for (const Item& it : player.dye) w.NetId(it), w.U8(it.prefix);
        for (const Item& it : player.inventory) w.NetId(it), w.I32(it.stack), w.U8(it.prefix), w.U8(it.favorited);
        for (const Item& it : player.miscEquips) w.NetId(it), w.U8(it.prefix);
        for (const Item& it : player.miscDyes) w.NetId(it), w.U8(it.prefix);
        for (size_t b = 0; b < 4; ++b)
            for (const Item& it : player.banks[b]) {
                w.NetId(it), w.I32(it.stack), w.U8(it.prefix);
                if (b == 3) w.U8(it.favorited);
            }
        uint8_t bits = 0;  // SaveTemporaryItemSlotContents
        for (size_t i = 0; i < held.size(); ++i)
            if (!held[i].IsAir()) bits = static_cast<uint8_t>(bits | (1u << i));
        w.U8(bits);
        for (size_t i = 0; i < held.size(); ++i)
            if (bits & (1u << i)) WriteSerialized(w, held[i]);
        for (size_t l = 0; l < 3; ++l) {  // EquipmentLoadout.Serialize
            for (const Item& it : player.loadoutArmor[l]) WriteSerialized(w, it);
            for (const Item& it : player.loadoutDye[l]) WriteSerialized(w, it);
        }
    }
    void ReadPlayer(Reader& r, Player& p) {
        for (Item& it : p.armor) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        for (Item& it : p.dye) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        auto checked = [this](int32_t id) { return id >= kItemCount ? Item{} : NetDefaults(id); };  // vira ar
        for (Item& it : p.inventory) {
            it = checked(r.Get<int32_t>());
            int32_t stack = r.Get<int32_t>();
            uint8_t prefix = r.Get<uint8_t>();
            bool fav = r.Get<uint8_t>() != 0;
            if (it.type) it.stack = stack, it.prefix = prefix, it.favorited = fav;
        }
        for (Item& it : p.miscEquips) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        for (Item& it : p.miscDyes) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        for (size_t b = 0; b < 4; ++b)
            for (Item& it : p.banks[b]) {
                it = checked(r.Get<int32_t>());
                int32_t stack = r.Get<int32_t>();
                uint8_t prefix = r.Get<uint8_t>();
                bool fav = b == 3 && r.Get<uint8_t>() != 0;
                if (it.type) it.stack = stack, it.prefix = prefix, it.favorited = fav;
            }
        uint8_t bits = r.Get<uint8_t>();
        for (size_t i = 0; i < p.temporary.size(); ++i)
            if (bits & (1u << i)) p.temporary[i] = ReadSerialized(r);
        for (size_t l = 0; l < 3; ++l) {
            for (Item& it : p.loadoutArmor[l]) it = ReadSerialized(r);
            for (Item& it : p.loadoutDye[l]) it = ReadSerialized(r);
        }
    }
    void WriteWorld(Writer& w) {  // WorldFile.SaveChests
        int16_t count = 0;
        for (const auto& c : chests) count = static_cast<int16_t>(count + (c ? 1 : 0));
        w.I16(count);
        for (const auto& c : chests) {
            if (!c) continue;
            w.I32(c->x);
            w.I32(c->y);
            for (const Item& it : c->item) {
                if (At(itemShouldNotBeInInventory, it.type)) {  // ItemID.Sets.ItemsThatShouldNotBeInInventory[type]
                    w.I16(0);
                    continue;
                }
                w.I16(static_cast<int16_t>(it.IsAir() ? 0 : it.stack));
                if (!it.IsAir()) w.NetId(it), w.U8(it.prefix);
            }
        }
    }
    void ReadWorld(Reader& r, std::vector<std::optional<Chest>>& out) {  // LoadChests
        int16_t count = r.Get<int16_t>();
        out.assign(16, std::nullopt);
        for (int16_t c = 0; c < count; ++c) {
            Chest chest;
            chest.x = r.Get<int32_t>();
            chest.y = r.Get<int32_t>();
            for (Item& it : chest.item) {
                int16_t stack = r.Get<int16_t>();
                if (stack <= 0) continue;
                it = NetDefaults(r.Get<int32_t>());
                it.stack = stack;
                it.prefix = r.Get<uint8_t>();
            }
            out[static_cast<size_t>(c)] = chest;
        }
    }

    template <typename WriteFn>
    bool Save(const SaveFile& file, WriteFn write) {
        Slots slots(*this, file.kind, true);
        if (persistence.beforeSave) persistence.beforeSave(file, slots);
        if (duringSave) duringSave();
        bool ok = true;
        int32_t maxId = 0;
        try {
            Writer w{std::string(), &maxId};
            write(w);
            bool fail = false;
            {
                std::lock_guard<std::mutex> lock(statsMutex);
                std::swap(fail, failNextSave);
            }
            if (fail) throw std::runtime_error("IOException: disco cheio");
            std::ofstream(file.path, std::ios::binary | std::ios::trunc) << w.out;
        } catch (const std::exception&) {
            ok = false;
        }
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            lastSavedMaxId = maxId;
            if (ok) worstSavedId = std::max(worstSavedId, maxId);
        }
        if (persistence.afterSave) persistence.afterSave(file, slots, ok);
        return ok;
    }
    template <typename ReadFn>
    static bool Load(const std::string& path, std::string* error, ReadFn read) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) *error = "arquivo não existe";
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        Reader r{ss.str()};
        try {
            read(r);
        } catch (const std::exception& e) {
            if (error) *error = e.what();
            return false;
        }
        return true;
    }
    void AfterLoad(const SaveFile& file) {
        Slots slots(*this, file.kind, false);
        if (persistence.afterLoad) persistence.afterLoad(file, slots);
    }
};
