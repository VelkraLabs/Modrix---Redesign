#include "tlmod/runtime/item_persistence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"
#include "json_writer.h"
#include "tlmod/runtime/content_registry.h"
#include "tlmod/runtime/log.h"

namespace tlmod::rt {

namespace {

constexpr const char* kLogSource = "saves";
// Inventário principal (0..49): 50..53 são moedas e 54..57 munição, que só
// aceitam certos itens. É para onde vai um item que perdeu o lugar.
constexpr int32_t kInventoryMain = 50;
// Espera máxima pela vez (outro save do mesmo tipo). Passou disso, segue sem a vez
// (avisa): melhor que travar o save do jogo se a ordem de locks dele surpreender.
constexpr std::chrono::milliseconds kSaveTurnWait{30000};

size_t KindIndex(SaveFile::Kind k) { return k == SaveFile::Kind::Player ? 0 : 1; }

struct ContainerName {
    ItemContainer container;
    const char* name;
};
constexpr ContainerName kContainerNames[] = {
    {ItemContainer::Inventory, "inventory"},
    {ItemContainer::Armor, "armor"},
    {ItemContainer::Dye, "dye"},
    {ItemContainer::MiscEquips, "misc_equips"},
    {ItemContainer::MiscDyes, "misc_dyes"},
    {ItemContainer::PiggyBank, "piggy_bank"},
    {ItemContainer::Safe, "safe"},
    {ItemContainer::DefendersForge, "defenders_forge"},
    {ItemContainer::VoidVault, "void_vault"},
    {ItemContainer::LoadoutArmor, "loadout_armor"},
    {ItemContainer::LoadoutDye, "loadout_dye"},
    {ItemContainer::Temporary, "temporary"},
    {ItemContainer::Chest, "chest"},
};

bool IsLoadout(ItemContainer c) { return c == ItemContainer::LoadoutArmor || c == ItemContainer::LoadoutDye; }

std::string Describe(const ItemSlot& s) {
    std::string out = ToString(s.container);
    if (IsLoadout(s.container)) out += "[" + std::to_string(s.loadout) + "]";
    if (s.container == ItemContainer::Chest)
        out += "(" + std::to_string(s.chestX) + "," + std::to_string(s.chestY) + ")";
    return out + "#" + std::to_string(s.index);
}

// Slot pedido primeiro; depois os equivalentes livres, em ordem de preferência.
std::vector<ItemSlot> Candidates(const ItemSlot& wanted, const ItemSlots& slots) {
    std::vector<ItemSlot> out{wanted};
    auto addAll = [&](ItemSlot base, int32_t limit) {
        int32_t n = slots.Count(base);
        if (limit >= 0) n = std::min(n, limit);
        for (int32_t i = 0; i < n; ++i) {
            base.index = i;
            if (!(base == wanted)) out.push_back(base);
        }
    };
    ItemSlot inventory;  // Inventory, índice 0
    switch (wanted.container) {
        case ItemContainer::Inventory:
            addAll(wanted, kInventoryMain);
            break;
        case ItemContainer::PiggyBank:
        case ItemContainer::Safe:
        case ItemContainer::DefendersForge:
        case ItemContainer::VoidVault:
            addAll(wanted, -1);
            addAll(inventory, kInventoryMain);
            break;
        case ItemContainer::Chest:
            addAll(wanted, -1);  // o mesmo baú; o mundo não tem "inventário"
            break;
        default:  // equipamento, loadouts e temporários: o slot é específico
            addAll(inventory, kInventoryMain);
            break;
    }
    return out;
}

std::optional<int64_t> Integer(const json::Value* v) {
    const double* d = v ? v->AsNumber() : nullptr;
    if (!d || std::floor(*d) != *d || std::fabs(*d) > 2147483647.0) return std::nullopt;
    return static_cast<int64_t>(*d);
}

}  // namespace

const char* ToString(ItemContainer c) {
    for (const ContainerName& n : kContainerNames)
        if (n.container == c) return n.name;
    return "?";
}

std::optional<ItemContainer> ParseItemContainer(const std::string& name) {
    for (const ContainerName& n : kContainerNames)
        if (name == n.name) return n.container;
    return std::nullopt;
}

bool IsPlayerContainer(ItemContainer c) { return c != ItemContainer::Chest; }

ItemPersistence::ItemPersistence(const ContentRegistry& registry) : registry_(registry) {}

bool ItemPersistence::Turn::Take(std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, wait, [this] { return !busy; })) return false;
    busy = true;
    return true;
}

void ItemPersistence::Turn::Give() {
    {
        std::lock_guard<std::mutex> lock(m);
        busy = false;
    }
    cv.notify_one();
}

void ItemPersistence::SetCloudDirectory(std::filesystem::path dir) { cloudDir_ = std::move(dir); }

std::filesystem::path ItemPersistence::SidecarPath(const SaveFile& file) const {
    if (file.cloud && !cloudDir_.empty()) {
        const char* sub = file.kind == SaveFile::Kind::Player ? "player" : "world";
        return cloudDir_ / sub / (std::filesystem::path(file.path).filename().string() + kSidecarSuffix);
    }
    return std::filesystem::path(file.path + kSidecarSuffix);
}

bool ItemPersistence::IsVanilla(int32_t type) const {
    // Negativos são variantes antigas de itens originais (netID < 0).
    const KindRange& r = registry_.Range(TLMOD_CONTENT_ITEM);
    return type < registry_.VanillaCount(TLMOD_CONTENT_ITEM).value_or(r.firstModId);
}

std::optional<ItemSlot> ItemPersistence::Place(ItemSlots& slots, const ItemSlot& wanted,
                                               const ItemStack& item) const {
    for (const ItemSlot& c : Candidates(wanted, slots)) {
        std::optional<ItemStack> cur = slots.Read(c);
        if (cur && cur->IsAir() && slots.Write(c, item)) return c;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void ItemPersistence::BeforeSave(const SaveFile& file, ItemSlots& slots) {
    if (file.path.empty()) return;
    bool hasTurn = turn_[KindIndex(file.kind)].Take(kSaveTurnWait);
    if (!hasTurn)
        Log(TLMOD_LOG_ERROR, kLogSource, file.path + ": outro save do mesmo tipo não terminou; seguindo sem esperar");
    std::vector<Stripped> stripped;
    slots.ForEach([&](const ItemSlot& slot, const ItemStack& item) {
        if (IsVanilla(item.type)) return;
        Stripped s;
        s.original = item;
        s.entry.slot = slot;
        s.entry.stack = item.stack;
        s.entry.prefix = item.prefix;
        s.entry.favorited = item.favorited;
        if (const ContentEntry* e = registry_.Get(TLMOD_CONTENT_ITEM, item.type)) s.entry.key = e->key;
        stripped.push_back(std::move(s));
    });
    for (const Stripped& s : stripped) {
        if (!slots.Write(s.entry.slot, ItemStack{}))
            Log(TLMOD_LOG_ERROR, kLogSource,
                file.path + ": não consegui esvaziar " + Describe(s.entry.slot) + "; o ID " +
                    std::to_string(s.original.type) + " vai para o arquivo do jogo");
        else if (s.entry.key.empty())
            Log(TLMOD_LOG_WARN, kLogSource,
                file.path + ": ID " + std::to_string(s.original.type) + " em " + Describe(s.entry.slot) +
                    " não é original nem de um mod carregado; fica fora do arquivo e só na memória");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    InFlight& mine = inFlight_[SaveKey{file.path, std::this_thread::get_id()}];
    for (Stripped& s : stripped) mine.stripped.push_back(std::move(s));
    mine.hasTurn = mine.hasTurn || hasTurn;
}

void ItemPersistence::AfterSave(const SaveFile& file, ItemSlots& slots, bool ok) {
    if (file.path.empty()) return;
    std::vector<Stripped> mine;
    bool hasTurn = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = inFlight_.find(SaveKey{file.path, std::this_thread::get_id()});
        if (it != inFlight_.end()) {
            mine = std::move(it->second.stripped);
            hasTurn = it->second.hasTurn;
            inFlight_.erase(it);
        }
    }
    // A vez do próximo save só depois de devolver os itens e gravar o paralelo.
    struct GiveTurn {
        Turn* turn;
        ~GiveTurn() {
            if (turn) turn->Give();
        }
    } giveTurn{hasTurn ? &turn_[KindIndex(file.kind)] : nullptr};

    // 1) Devolve os itens. O save pode rodar fora da thread do jogo: se o
    //    jogo pôs outra coisa no slot enquanto isso, o item vai para um slot
    //    livre equivalente, ou fica guardado como órfão. Nunca sobrescreve.
    std::vector<SavedModItem> inGame, noSpace;
    for (Stripped& s : mine) {
        std::optional<ItemStack> cur = slots.Read(s.entry.slot);
        bool back = cur && cur->IsAir() && slots.Write(s.entry.slot, s.original);
        if (!back) {
            if (std::optional<ItemSlot> where = Place(slots, s.entry.slot, s.original)) {
                Log(TLMOD_LOG_INFO, kLogSource,
                    file.path + ": " + (s.entry.key.empty() ? std::to_string(s.original.type) : s.entry.key) +
                        " mudou de " + Describe(s.entry.slot) + " para " + Describe(*where) +
                        " (o slot foi ocupado durante o save)");
                back = true;
            }
        }
        if (s.entry.key.empty()) {
            if (!back)
                Log(TLMOD_LOG_ERROR, kLogSource,
                    file.path + ": ID " + std::to_string(s.original.type) + " sem chave e sem lugar; perdido");
            continue;
        }
        // A posição gravada é a do momento do save: é a que o arquivo do jogo
        // deixou vazia. Se estiver ocupada no próximo load, o item procura outra.
        if (back) {
            inGame.push_back(s.entry);
        } else {
            s.entry.orphan = "no_space";
            Log(TLMOD_LOG_WARN, kLogSource,
                file.path + ": " + s.entry.key + " sem lugar depois do save; fica só no arquivo paralelo");
            noSpace.push_back(s.entry);
        }
    }

    // 2) Arquivo paralelo: o que acabou de sair do arquivo do jogo, o que
    //    outros saves deste arquivo em andamento tiraram, e os órfãos.
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SavedModItem>& orphans = OrphansLocked(file);
    for (SavedModItem& e : noSpace) orphans.push_back(std::move(e));
    if (!ok) {
        // O arquivo do jogo não mudou: o paralelo também não.
        Log(TLMOD_LOG_WARN, kLogSource, file.path + ": o save falhou; arquivo paralelo mantido");
        return;
    }
    std::vector<SavedModItem> out = std::move(inGame);
    // (só se alguém seguiu sem esperar a vez: ver kSaveTurnWait)
    for (const auto& [key, others] : inFlight_)
        if (key.path == file.path)
            for (const Stripped& s : others.stripped)
                if (!s.entry.key.empty()) out.push_back(s.entry);
    out.insert(out.end(), orphans.begin(), orphans.end());

    std::filesystem::path sidecar = SidecarPath(file);
    std::error_code ec;
    if (out.empty()) {
        std::filesystem::remove(sidecar, ec);  // nada de mod neste arquivo
        return;
    }
    std::string error;
    if (!WriteSidecar(sidecar, file.kind, out, &error))
        Log(TLMOD_LOG_ERROR, kLogSource, error);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

void ItemPersistence::AfterLoad(const SaveFile& file, ItemSlots& slots) {
    if (file.path.empty()) return;
    std::vector<SavedModItem> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries = ReadOrQuarantine(file);
    }
    std::vector<SavedModItem> kept;
    size_t placed = 0, moved = 0;
    for (SavedModItem& e : entries) {
        std::optional<TLModId> id = registry_.Find(TLMOD_CONTENT_ITEM, e.key);
        if (!id) {
            e.orphan = "missing_mod";
            kept.push_back(std::move(e));
            continue;
        }
        ItemStack item{*id, std::max(1, e.stack), e.prefix, e.favorited};
        const ContentEntry* def = registry_.Get(TLMOD_CONTENT_ITEM, *id);
        if (def && def->Item() && item.stack > def->Item()->maxStack) {
            Log(TLMOD_LOG_WARN, kLogSource,
                file.path + ": " + e.key + " com pilha " + std::to_string(item.stack) + " acima do máximo (" +
                    std::to_string(def->Item()->maxStack) + "); ajustada");
            item.stack = def->Item()->maxStack;
        }
        std::optional<ItemSlot> where = Place(slots, e.slot, item);
        if (!where) {
            e.orphan = "no_space";
            Log(TLMOD_LOG_WARN, kLogSource, file.path + ": " + e.key + " sem lugar livre; continua guardado");
            kept.push_back(std::move(e));
            continue;
        }
        ++placed;
        if (!(*where == e.slot)) {
            ++moved;
            Log(TLMOD_LOG_INFO, kLogSource,
                file.path + ": " + e.key + " foi para " + Describe(*where) + " (" + Describe(e.slot) + " ocupado)");
        }
    }
    if (!entries.empty())
        Log(TLMOD_LOG_INFO, kLogSource,
            file.path + ": " + std::to_string(placed) + " itens de mod reinseridos (" + std::to_string(moved) +
                " em outro slot), " + std::to_string(kept.size()) + " guardados como órfãos");
    std::lock_guard<std::mutex> lock(mutex_);
    orphans_[file.path] = std::move(kept);
}

std::vector<SavedModItem> ItemPersistence::Orphans(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orphans_.find(path);
    return it == orphans_.end() ? std::vector<SavedModItem>{} : it->second;
}

std::vector<SavedModItem>& ItemPersistence::OrphansLocked(const SaveFile& file) {
    auto it = orphans_.find(file.path);
    if (it != orphans_.end()) return it->second;
    // Arquivo salvo sem ter passado pelo AfterLoad (o jogo o leu antes de os
    // ganchos existirem, ou é um arquivo novo): tudo o que está no paralelo
    // está fora do jogo e precisa continuar lá.
    std::vector<SavedModItem> entries = ReadOrQuarantine(file);
    for (SavedModItem& e : entries)
        if (e.orphan.empty()) e.orphan = registry_.Find(TLMOD_CONTENT_ITEM, e.key) ? "not_loaded" : "missing_mod";
    return orphans_[file.path] = std::move(entries);
}

std::vector<SavedModItem> ItemPersistence::ReadOrQuarantine(const SaveFile& file) {
    std::filesystem::path sidecar = SidecarPath(file);
    std::string error;
    std::optional<std::vector<SavedModItem>> entries = ReadSidecar(sidecar, &error);
    if (entries) return std::move(*entries);
    // Ilegível: guarda uma cópia para recuperação manual em vez de sobrescrever.
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    std::filesystem::path bad = sidecar;
    bad += ".invalid-" + std::to_string(stamp);
    std::error_code ec;
    std::filesystem::rename(sidecar, bad, ec);
    Log(TLMOD_LOG_ERROR, kLogSource,
        error + (ec ? "" : " (movido para " + bad.filename().string() + ")"));
    return {};
}

// ---------------------------------------------------------------------------
// Formato do arquivo paralelo
//
//   {"tlmod_items": 1, "kind": "player", "items": [
//     {"key": "mod.id:nome", "container": "inventory", "slot": 3, "stack": 5, "prefix": 7,
//      "favorited": true},
//     {"key": ..., "container": "loadout_armor", "loadout": 1, "slot": 0, ...},
//     {"key": ..., "container": "chest", "x": 120, "y": 340, "slot": 5, ..., "orphan": "missing_mod"}
//   ]}
// ---------------------------------------------------------------------------

std::optional<std::vector<SavedModItem>> ItemPersistence::ReadSidecar(const std::filesystem::path& file,
                                                                      std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<std::vector<SavedModItem>> {
        if (error) *error = file.string() + ": " + msg;
        return std::nullopt;
    };
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return std::vector<SavedModItem>{};
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in && !in.eof()) return fail("não foi possível ler");

    std::string parseError;
    std::optional<json::Value> root = json::Parse(ss.str(), &parseError);
    if (!root || !root->AsObject()) return fail("JSON inválido (" + parseError + ")");
    std::optional<int64_t> version = Integer(root->Get("tlmod_items"));
    if (!version || *version != kFormatVersion)
        return fail("versão " + (version ? std::to_string(*version) : std::string("?")) + " não suportada");
    const json::Value* items = root->Get("items");
    if (!items || !items->AsArray()) return fail("\"items\" ausente");

    std::vector<SavedModItem> out;
    size_t n = 0;
    for (const json::Value& v : *items->AsArray()) {
        std::string where = "items[" + std::to_string(n++) + "]";
        const std::string* key = v.Get("key") ? v.Get("key")->AsString() : nullptr;
        const std::string* container = v.Get("container") ? v.Get("container")->AsString() : nullptr;
        std::optional<int64_t> slot = Integer(v.Get("slot"));
        std::optional<int64_t> stack = Integer(v.Get("stack"));
        std::optional<int64_t> prefix = v.Get("prefix") ? Integer(v.Get("prefix")) : std::optional<int64_t>(0);
        if (!key || key->find(':') == std::string::npos) return fail(where + ".key inválida");
        std::optional<ItemContainer> c = container ? ParseItemContainer(*container) : std::nullopt;
        if (!c) return fail(where + ".container inválido");
        if (!slot || *slot < 0 || !stack || !prefix || *prefix < 0 || *prefix > 255)
            return fail(where + ": slot/stack/prefix inválidos");

        SavedModItem e;
        e.key = *key;
        e.slot.container = *c;
        e.slot.index = static_cast<int32_t>(*slot);
        if (IsLoadout(*c)) {
            std::optional<int64_t> lo = Integer(v.Get("loadout"));
            if (!lo || *lo < 0) return fail(where + ".loadout inválido");
            e.slot.loadout = static_cast<int32_t>(*lo);
        }
        if (*c == ItemContainer::Chest) {
            std::optional<int64_t> x = Integer(v.Get("x")), y = Integer(v.Get("y"));
            if (!x || !y) return fail(where + ": x/y do baú inválidos");
            e.slot.chestX = static_cast<int32_t>(*x);
            e.slot.chestY = static_cast<int32_t>(*y);
        }
        e.stack = static_cast<int32_t>(*stack);
        e.prefix = static_cast<uint8_t>(*prefix);
        const json::Value* fav = v.Get("favorited");
        e.favorited = fav && fav->AsBool() && *fav->AsBool();
        const json::Value* orphan = v.Get("orphan");
        if (orphan && orphan->AsString()) e.orphan = *orphan->AsString();
        out.push_back(std::move(e));
    }
    return out;
}

bool ItemPersistence::WriteSidecar(const std::filesystem::path& file, SaveFile::Kind kind,
                                   const std::vector<SavedModItem>& items, std::string* error) {
    JsonWriter w;
    w.BeginObject();
    w.Key("tlmod_items").Int(kFormatVersion);
    w.Key("kind").String(kind == SaveFile::Kind::Player ? "player" : "world");
    w.Key("items").BeginArray();
    for (const SavedModItem& e : items) {
        w.BeginObject();
        w.Key("key").String(e.key);
        w.Key("container").String(ToString(e.slot.container));
        if (IsLoadout(e.slot.container)) w.Key("loadout").Int(e.slot.loadout);
        if (e.slot.container == ItemContainer::Chest) {
            w.Key("x").Int(e.slot.chestX);
            w.Key("y").Int(e.slot.chestY);
        }
        w.Key("slot").Int(e.slot.index);
        w.Key("stack").Int(e.stack);
        w.Key("prefix").Int(e.prefix);
        if (e.favorited) w.Key("favorited").Bool(true);
        if (!e.orphan.empty()) w.Key("orphan").String(e.orphan);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    // tmp + rename: atômico no mesmo sistema de arquivos.
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << w.str() << '\n';
        out.flush();
        if (!out) {
            if (error) *error = "não foi possível gravar " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "não foi possível salvar " + file.string();
        return false;
    }
    return true;
}

}  // namespace tlmod::rt
