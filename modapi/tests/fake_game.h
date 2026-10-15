// GameAdapter falso para testes: simula players, o fluxo de Player.Hurt, NPCs
// e o save/load de personagem e mundo.
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
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

    // --- Itens e saves -------------------------------------------------------------
    // Mesmo layout de item do jogo (Player.InternalSavePlayerFile, WorldFile.SaveChests:
    // netID int32, stack, prefix byte) e as mesmas reações a um ID fora de ItemID.Count
    // (5456): na leitura do .plr, armor/dye/miscEquips/miscDyes, temporários e loadouts
    // chamam netDefaults(id) e estouram (o personagem não carrega); inventário e bancos
    // trocam por ar (o item some). SaveChests estoura (o mundo não salva) e LoadChests
    // também. Os baús voltam do load com índices novos (só os não nulos, em ordem).
    static constexpr int32_t kItemCount = 5456;
    using ItemContainer = tlmod::rt::ItemContainer;
    using ItemSlot = tlmod::rt::ItemSlot;
    using ItemStack = tlmod::rt::ItemStack;
    using SaveFile = tlmod::rt::SaveFile;

    struct Item {
        int32_t type = 0, stack = 0;
        uint8_t prefix = 0;
        bool favorited = false;
        bool IsAir() const { return type == 0 || stack <= 0; }
    };
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
                    if (Item* it = Ref(s); it && !it->IsAir()) fn(s, ItemStack{it->type, it->stack, it->prefix, it->favorited});
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
            return ItemStack{it->type, it->stack, it->prefix, it->favorited};
        }
        bool Write(const ItemSlot& s, const ItemStack& v) override {
            Item* it = Ref(s);
            if (!it) return false;
            *it = Item{v.type, v.stack, v.prefix, v.favorited};
            return true;
        }
        int32_t Count(const ItemSlot& s) const override {
            ItemSlot probe = s;
            int32_t n = 0;
            for (probe.index = 0; Ref(probe); ++probe.index) ++n;
            return n;
        }

    private:
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
    struct Writer {
        std::string out;
        int32_t* maxId;
        void I32(int32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
        void I16(int16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); }
        void U8(uint8_t v) { out.push_back(static_cast<char>(v)); }
        void NetId(const Item& it) {
            if (it.type > *maxId) *maxId = it.type;
            I32(it.type);
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
    // Item.netDefaults/SetDefaults: o jogo indexa arrays de tamanho ItemID.Count.
    static Item NetDefaults(int32_t id) {
        if (id >= kItemCount) throw std::out_of_range("IndexOutOfRangeException: netDefaults(" + std::to_string(id) + ")");
        return Item{id, id == 0 ? 0 : 1, 0, false};
    }
    static void WriteSerialized(Writer& w, const Item& it) {  // Item.Serialize
        w.NetId(it);
        w.I32(it.stack);
        w.U8(it.prefix);
    }
    static Item ReadSerialized(Reader& r) {  // Item.DeserializeFrom
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
    static void ReadPlayer(Reader& r, Player& p) {
        for (Item& it : p.armor) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        for (Item& it : p.dye) it = NetDefaults(r.Get<int32_t>()), it.prefix = r.Get<uint8_t>();
        auto checked = [](int32_t id) { return id >= kItemCount ? Item{} : NetDefaults(id); };  // vira ar
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
    void WriteWorld(Writer& w) const {  // WorldFile.SaveChests
        int16_t count = 0;
        for (const auto& c : chests) count = static_cast<int16_t>(count + (c ? 1 : 0));
        w.I16(count);
        for (const auto& c : chests) {
            if (!c) continue;
            w.I32(c->x);
            w.I32(c->y);
            for (const Item& it : c->item) {
                // ItemID.Sets.ItemsThatShouldNotBeInInventory[item.type]
                if (it.type >= kItemCount) throw std::out_of_range("IndexOutOfRangeException em SaveChests");
                w.I16(static_cast<int16_t>(it.IsAir() ? 0 : it.stack));
                if (!it.IsAir()) w.NetId(it), w.U8(it.prefix);
            }
        }
    }
    static void ReadWorld(Reader& r, std::vector<std::optional<Chest>>& out) {  // LoadChests
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
