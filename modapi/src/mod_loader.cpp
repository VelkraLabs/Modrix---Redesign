#include "modrix/runtime/mod_loader.h"

#include <algorithm>
#include <iterator>
#include <cstddef>
#include <map>
#include <mutex>
#include <queue>
#include <set>

#include "dynlib.h"
#include "json.h"
#include "modrix/runtime/game_adapter.h"
#include "modrix/runtime/log.h"

// Buffer em que um provedor escreve o JSON. Opaco do lado do mod.
struct ModrixBuffer {
    std::string data;
};

// Contexto entregue ao mod. Opaco do lado do mod (modrix_api.h só declara).
struct ModrixContext {
    modrix::rt::ModLoader* loader;
    modrix::rt::ModLoader::Impl* impl;
    size_t index;             // em ModLoader::Mods()
    bool initializing = false;
    std::string version;      // cache para mod_version()
    std::string dir;          // cache para mod_dir()
};

namespace modrix::rt {

const char* ToString(ModState s) {
    switch (s) {
        case ModState::Discovered: return "descoberto";
        case ModState::Rejected: return "rejeitado";
        case ModState::Loaded: return "carregado";
        case ModState::Failed: return "falhou";
        case ModState::Skipped: return "pulado";
    }
    return "?";
}

struct ModLoader::Impl {
    struct HurtSub {
        ModrixContext* ctx;
        ModrixPlayerHurtFn fn;
        void* user;
    };

    ModLoader* self;
    GameAdapter* game;
    ModrixHostApi api{};
    std::vector<std::unique_ptr<ModrixContext>> contexts;
    std::vector<void*> libraries;  // nunca fechadas: hooks/eventos podem apontar para elas
    std::vector<HurtSub> hurtSubs;

    // Provedores de dados para a interface ("mod_id/channel").
    struct Provider {
        std::string key;
        ModrixProviderFn fn;
        void* user;
        std::unique_ptr<std::mutex> mutex;  // serializa chamadas ao mesmo provedor
    };
    std::vector<Provider> providers;

    const ModRecord& Mod(const ModrixContext* ctx) const { return self->mods_[ctx->index]; }
    const std::string& ModId(const ModrixContext* ctx) const { return Mod(ctx).manifest.id; }

    bool DispatchHurt(ModrixPlayerHurt& ev, bool after) {
        for (const HurtSub& s : hurtSubs) {
            bool keep = s.fn(s.user, &ev, after);
            if (!after && !keep) return false;  // um mod cancelou o golpe
        }
        return true;
    }

    // ---- NPCs de mod ----------------------------------------------------------
    // Tabela lateral: para cada slot de Main.npc, qual NPC de mod está ali
    // (MODRIX_INVALID_ID = NPC original) e o estado privado local[] dele.
    // É o "registro de spawns": preenchido no pós-NewNPC, limpo no SetDefaults.
    struct NpcSlot {
        ModrixId type = MODRIX_INVALID_ID;
        float local[8] = {};
    };
    std::vector<NpcSlot> npcSlots;
    uint64_t modNpcSpawns = 0;

    const NpcDefinition* ModNpcDef(int32_t type) const {
        const ContentEntry* e = self->registry_.Get(MODRIX_CONTENT_NPC, type);
        return e ? e->Npc() : nullptr;
    }
    bool ValidSlot(int32_t index) const {
        return index >= 0 && static_cast<size_t>(index) < npcSlots.size();
    }
    bool ReadModNpc(int32_t index, ModrixNpcState& st) const {
        if (!game->ReadNpc(index, st)) return false;
        st.index = index;
        st.type = npcSlots[static_cast<size_t>(index)].type;  // o jogo só vê a casca
        std::copy(std::begin(npcSlots[static_cast<size_t>(index)].local),
                  std::end(npcSlots[static_cast<size_t>(index)].local), st.local);
        return true;
    }
    void WriteModNpc(int32_t index, const ModrixNpcState& st) {
        game->WriteNpc(index, st);
        std::copy(std::begin(st.local), std::end(st.local), npcSlots[static_cast<size_t>(index)].local);
    }

    // Pré-NewNPC: ID de mod -> casca original. O ID de mod nunca chega ao jogo.
    int32_t RemapSpawnType(int32_t type) const {
        const NpcDefinition* def = ModNpcDef(type);
        return def ? def->shellType : type;
    }

    // Pós-NewNPC: registra o spawn e aplica a definição do mod.
    void OnNpcSpawned(int32_t index, int32_t requested, int32_t spawned) {
        const NpcDefinition* def = ModNpcDef(requested);
        if (!def) return;  // NPC original: nada a fazer (ponto para eventos de spawn no futuro)
        const ContentEntry* e = self->registry_.Get(MODRIX_CONTENT_NPC, requested);
        if (!ValidSlot(index)) {
            Log(MODRIX_LOG_WARN, e->modId, "spawn de " + e->key + " falhou: sem slot livre");
            return;
        }
        npcSlots[static_cast<size_t>(index)] = NpcSlot{requested, {}};
        ++modNpcSpawns;
        Log(MODRIX_LOG_DEBUG, e->modId,
            "spawn " + e->key + " no slot " + std::to_string(index) + " (casca " + std::to_string(spawned) + ")");

        ModrixNpcState st{};
        if (!ReadModNpc(index, st)) return;
        if (def->lifeMax > 0) st.life = st.life_max = def->lifeMax;
        if (def->damage >= 0) st.damage = def->damage;
        if (def->defense >= 0) st.defense = def->defense;
        if (def->onSpawn) def->onSpawn(def->user, &st);
        WriteModNpc(index, st);
    }

    // Pré-SetDefaults: o slot vai virar outro NPC. Desmarca; se for um spawn de
    // mod, OnNpcSpawned (pós-NewNPC) marca de novo logo depois.
    void OnNpcSetDefaults(int32_t index, int32_t /*newType*/) {
        if (ValidSlot(index)) npcSlots[static_cast<size_t>(index)] = NpcSlot{};
    }

    // Pré-AI, por tick: só NPCs de mod com on_ai pagam o custo de ler/escrever.
    bool OnNpcAi(int32_t index) {
        if (!ValidSlot(index)) return false;
        ModrixId type = npcSlots[static_cast<size_t>(index)].type;
        if (type == MODRIX_INVALID_ID) return false;
        const NpcDefinition* def = ModNpcDef(type);
        if (!def || !def->onAi) return false;
        ModrixNpcState st{};
        if (!ReadModNpc(index, st)) return false;
        bool replace = def->onAi(def->user, &st);
        WriteModNpc(index, st);
        return replace;
    }
};

namespace {

using Impl = ModLoader::Impl;

Impl* ImplOf(const ModrixContext* ctx) { return ctx ? ctx->impl : nullptr; }

// ---- Implementação de ModrixHostApi -------------------------------------
// Todas validam o contexto; nenhum erro de mod derruba o runtime.

const char* Api_ModId(const ModrixContext* ctx) {
    return ctx ? ImplOf(ctx)->ModId(ctx).c_str() : "";
}
const char* Api_ModVersion(const ModrixContext* ctx) { return ctx ? ctx->version.c_str() : ""; }
const char* Api_ModDir(const ModrixContext* ctx) { return ctx ? ctx->dir.c_str() : ""; }

void Api_Log(const ModrixContext* ctx, ModrixLogLevel level, const char* msg) {
    Log(level, ctx ? ImplOf(ctx)->ModId(ctx) : "?", msg ? msg : "");
}

constexpr uint32_t kItemDefV1Size = static_cast<uint32_t>(offsetof(ModrixItemDef, use_time) + sizeof(int32_t));

ModrixId Api_RegisterItem(ModrixContext* ctx, const ModrixItemDef* def) {
    if (!ctx) return MODRIX_INVALID_ID;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(MODRIX_LOG_ERROR, mod.manifest.id, "register_item: " + msg);
        return MODRIX_INVALID_ID;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante modrix_mod_init");
    if (!def || def->struct_size < kItemDefV1Size) return err("ModrixItemDef nulo ou struct_size inválido");
    if (!def->name) return err("name é obrigatório");
    std::string name = def->name;
    if (!mod.manifest.Declares(MODRIX_CONTENT_ITEM, name))
        return err("\"" + name + "\" não está declarado em content.items do manifest.json");
    if (!def->display_name || !*def->display_name) return err("display_name é obrigatório");
    if (def->max_stack < 1 || def->width <= 0 || def->height <= 0 || def->use_time < 0)
        return err("max_stack/width/height devem ser > 0 e use_time >= 0");

    ItemDefinition item;
    item.displayName = def->display_name;
    item.tooltip = def->tooltip ? def->tooltip : "";
    if (def->texture) {
        if (!IsSafeRelativePath(def->texture))
            return err("texture deve ser caminho relativo dentro da pasta do mod");
        item.texture = (mod.manifest.dir / def->texture).lexically_normal().string();
    }
    item.width = def->width;
    item.height = def->height;
    item.maxStack = def->max_stack;
    item.value = def->value;
    item.rarity = def->rarity;
    item.damage = def->damage;
    item.useTime = def->use_time;

    std::string error;
    auto id = impl->self->Registry().Register(MODRIX_CONTENT_ITEM, mod.manifest.id, name,
                                              std::move(item), &error);
    if (!id) return err(error);
    Log(MODRIX_LOG_INFO, mod.manifest.id,
        "item " + mod.manifest.id + ":" + name + " -> ID " + std::to_string(*id));
    return *id;
}

ModrixId Api_FindContent(const ModrixContext* ctx, ModrixContentKind kind, const char* key) {
    if (!ctx || !key) return MODRIX_INVALID_ID;
    auto id = ImplOf(ctx)->self->Registry().Find(kind, key);
    return id ? *id : MODRIX_INVALID_ID;
}

bool Api_SubscribePlayerHurt(ModrixContext* ctx, ModrixPlayerHurtFn fn, void* user) {
    if (!ctx || !fn) return false;
    Impl* impl = ImplOf(ctx);
    if (!ctx->initializing) {
        Log(MODRIX_LOG_ERROR, impl->ModId(ctx), "subscribe_player_hurt: só durante modrix_mod_init");
        return false;
    }
    impl->hurtSubs.push_back({ctx, fn, user});
    return true;
}

ModrixPlayer Api_LocalPlayer(const ModrixContext* ctx) {
    return ctx ? ImplOf(ctx)->game->LocalPlayer() : MODRIX_NO_PLAYER;
}

int32_t Api_PlayerLife(const ModrixContext* ctx, ModrixPlayer p) {
    return ctx ? ImplOf(ctx)->game->PlayerLife(p) : -1;
}

constexpr uint32_t kNpcDefV1Size = static_cast<uint32_t>(offsetof(ModrixNpcDef, on_ai) + sizeof(ModrixNpcAiFn));

ModrixId Api_RegisterNpc(ModrixContext* ctx, const ModrixNpcDef* def) {
    if (!ctx) return MODRIX_INVALID_ID;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(MODRIX_LOG_ERROR, mod.manifest.id, "register_npc: " + msg);
        return MODRIX_INVALID_ID;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante modrix_mod_init");
    if (!def || def->struct_size < kNpcDefV1Size) return err("ModrixNpcDef nulo ou struct_size inválido");
    if (!def->name) return err("name é obrigatório");
    std::string name = def->name;
    if (!mod.manifest.Declares(MODRIX_CONTENT_NPC, name))
        return err("\"" + name + "\" não está declarado em content.npcs do manifest.json");
    if (!def->display_name || !*def->display_name) return err("display_name é obrigatório");

    // A casca tem de ser um NPC original existente: é ela que o jogo vê.
    ContentRegistry& reg = impl->self->Registry();
    int32_t vanilla = reg.VanillaCount(MODRIX_CONTENT_NPC).value_or(0);
    if (def->shell_type <= 0 || def->shell_type >= vanilla)
        return err("shell_type " + std::to_string(def->shell_type) + " não é um NPC original (1.." +
                   std::to_string(vanilla - 1) + ")");

    NpcDefinition npc;
    npc.displayName = def->display_name;
    npc.shellType = def->shell_type;
    npc.lifeMax = def->life_max;
    npc.damage = def->damage;
    npc.defense = def->defense;
    npc.boss = def->boss;
    npc.user = def->user;
    npc.onSpawn = def->on_spawn;
    npc.onAi = def->on_ai;

    std::string error;
    auto id = reg.Register(MODRIX_CONTENT_NPC, mod.manifest.id, name, std::move(npc), &error);
    if (!id) return err(error);
    Log(MODRIX_LOG_INFO, mod.manifest.id,
        "npc " + mod.manifest.id + ":" + name + " -> ID " + std::to_string(*id) + " (casca " +
            std::to_string(def->shell_type) + ")");
    return *id;
}

int32_t ToTri(std::optional<bool> v) { return v ? (*v ? 1 : 0) : -1; }

int32_t Api_InWorld(const ModrixContext* ctx) { return ctx ? ToTri(ImplOf(ctx)->game->InWorld()) : -1; }

int32_t Api_Progress(const ModrixContext* ctx, ModrixProgress flag) {
    if (!ctx || flag < 0 || flag >= MODRIX_PROGRESS_COUNT) return -1;
    GameAdapter* game = ImplOf(ctx)->game;
    // Flags só fazem sentido com um mundo carregado; no menu ficam com o
    // valor do último mundo (ou lixo), então a resposta é "desconhecido".
    if (game->InWorld() != std::optional<bool>(true)) return -1;
    return ToTri(game->Progress(flag));
}

bool Api_RegisterProvider(ModrixContext* ctx, const char* channel, ModrixProviderFn fn, void* user) {
    if (!ctx) return false;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(MODRIX_LOG_ERROR, mod.manifest.id, "register_provider: " + msg);
        return false;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante modrix_mod_init");
    if (!channel || !fn) return err("channel e fn são obrigatórios");
    std::string ch = channel;
    if (std::find(mod.manifest.provides.begin(), mod.manifest.provides.end(), ch) == mod.manifest.provides.end())
        return err("\"" + ch + "\" não está declarado em \"provides\" do manifest.json");
    std::string key = mod.manifest.id + "/" + ch;
    for (const auto& p : impl->providers)
        if (p.key == key) return err("canal \"" + ch + "\" já registrado");
    impl->providers.push_back({key, fn, user, std::make_unique<std::mutex>()});
    return true;
}

void Api_BufferAppend(ModrixBuffer* out, const char* data, size_t len) {
    if (out && data) out->data.append(data, len);
}

int32_t Api_SpawnNpc(ModrixContext* ctx, ModrixId type, float x, float y) {
    if (!ctx) return -1;
    Impl* impl = ImplOf(ctx);
    const ContentRegistry& reg = impl->self->Registry();
    int32_t vanilla = reg.VanillaCount(MODRIX_CONTENT_NPC).value_or(0);
    bool known = reg.IsModId(MODRIX_CONTENT_NPC, type) || (type > 0 && type < vanilla);
    if (!known) {
        Log(MODRIX_LOG_ERROR, impl->ModId(ctx), "spawn_npc: tipo " + std::to_string(type) + " desconhecido");
        return -1;
    }
    // Passa pelo NewNPC do jogo; o remap para a casca acontece no gancho.
    return impl->game->SpawnNpc(type, x, y);
}

}  // namespace

// ---- ModLoader -----------------------------------------------------------

ModLoader::ModLoader(GameAdapter* game) : game_(game), registry_(game), impl_(std::make_unique<Impl>()) {
    impl_->self = this;
    impl_->game = game;
    ModrixHostApi& a = impl_->api;
    a.api_version = MODRIX_API_VERSION;
    a.struct_size = sizeof(ModrixHostApi);
    a.mod_id = &Api_ModId;
    a.mod_version = &Api_ModVersion;
    a.mod_dir = &Api_ModDir;
    a.log = &Api_Log;
    a.register_item = &Api_RegisterItem;
    a.find_content = &Api_FindContent;
    a.subscribe_player_hurt = &Api_SubscribePlayerHurt;
    a.local_player = &Api_LocalPlayer;
    a.player_life = &Api_PlayerLife;
    a.register_npc = &Api_RegisterNpc;
    a.spawn_npc = &Api_SpawnNpc;
    a.in_world = &Api_InWorld;
    a.progress = &Api_Progress;
    a.register_provider = &Api_RegisterProvider;
    a.buffer_append = &Api_BufferAppend;

    Impl* impl = impl_.get();
    game_->SetPlayerHurtHandler([impl](ModrixPlayerHurt& ev, bool after) { return impl->DispatchHurt(ev, after); });

    impl->npcSlots.resize(static_cast<size_t>(std::max(0, game_->MaxNpcs())));
    GameAdapter::NpcHooks hooks;
    hooks.remapSpawnType = [impl](int32_t t) { return impl->RemapSpawnType(t); };
    hooks.onSpawned = [impl](int32_t i, int32_t req, int32_t sp) { impl->OnNpcSpawned(i, req, sp); };
    hooks.onSetDefaults = [impl](int32_t i, int32_t t) { impl->OnNpcSetDefaults(i, t); };
    hooks.onAi = [impl](int32_t i) { return impl->OnNpcAi(i); };
    game_->SetNpcHooks(std::move(hooks));
}

ModLoader::~ModLoader() {
    game_->SetPlayerHurtHandler(nullptr);
    game_->SetNpcHooks({});
}

std::vector<std::string> ModLoader::Providers() const {
    std::vector<std::string> keys;
    for (const auto& p : impl_->providers) keys.push_back(p.key);
    return keys;
}

std::optional<std::string> ModLoader::Query(const std::string& modId, const std::string& channel,
                                            std::string* error) const {
    auto fail = [&](const std::string& msg) -> std::optional<std::string> {
        if (error) *error = msg;
        return std::nullopt;
    };
    std::string key = modId + "/" + channel;
    for (const auto& p : impl_->providers) {
        if (p.key != key) continue;
        ModrixBuffer buf;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(*p.mutex);
            ok = p.fn(p.user, &buf);
        }
        if (!ok) return fail(key + ": o provedor devolveu erro");
        // A interface recebe sempre JSON válido: um mod com bug não a quebra.
        std::string jsonError;
        if (!json::Parse(buf.data, &jsonError)) {
            Log(MODRIX_LOG_ERROR, modId, "provedor " + channel + " gerou JSON inválido: " + jsonError);
            return fail(key + ": " + jsonError);
        }
        return std::move(buf.data);
    }
    return fail("canal " + key + " não existe");
}

ModrixId ModLoader::NpcInSlot(int32_t index) const {
    return impl_->ValidSlot(index) ? impl_->npcSlots[static_cast<size_t>(index)].type : MODRIX_INVALID_ID;
}

const ModrixHostApi& ModLoader::HostApi() const { return impl_->api; }

const ModRecord* ModLoader::Find(const std::string& id) const {
    for (const ModRecord& m : mods_)
        if (m.manifest.id == id) return &m;
    return nullptr;
}

void ModLoader::DiscoverDirectory(const std::filesystem::path& modsDir) {
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (const auto& e : std::filesystem::directory_iterator(modsDir, ec))
        if (e.is_directory(ec) && std::filesystem::exists(e.path() / "manifest.json", ec))
            dirs.push_back(e.path());
    if (ec) Log(MODRIX_LOG_WARN, "loader", "não foi possível listar " + modsDir.string() + ": " + ec.message());
    std::sort(dirs.begin(), dirs.end());

    for (const auto& dir : dirs) {
        ModRecord rec;
        std::string error;
        auto m = LoadManifest(dir, &error);
        if (m) {
            rec.manifest = std::move(*m);
            if (const ModRecord* other = Find(rec.manifest.id)) {
                rec.state = ModState::Rejected;
                rec.error = "id duplicado (também em " + other->manifest.dir.string() + ")";
            }
        } else {
            rec.manifest.id = dir.filename().string();  // só para o relatório
            rec.manifest.dir = dir;
            rec.state = ModState::Rejected;
            rec.error = "manifest.json: " + error;
        }
        mods_.push_back(std::move(rec));
    }
}

void ModLoader::AddBuiltin(ModManifest manifest, ModrixModInitFn init) {
    ModRecord rec;
    rec.manifest = std::move(manifest);
    rec.builtinInit = init;
    if (Find(rec.manifest.id)) {
        rec.state = ModState::Rejected;
        rec.error = "id duplicado";
    }
    mods_.push_back(std::move(rec));
}

std::vector<std::string> ModLoader::ResolveOrder(std::vector<ModRecord>& mods) {
    auto reject = [](ModRecord& m, const std::string& why) {
        m.state = ModState::Rejected;
        m.error = why;
    };
    for (ModRecord& m : mods) {
        if (m.state == ModState::Discovered && m.manifest.apiVersion != MODRIX_API_VERSION)
            reject(m, "modrix_api " + std::to_string(m.manifest.apiVersion) + " incompatível (runtime: " +
                          std::to_string(MODRIX_API_VERSION) + ")");
    }

    // Rejeições se propagam: se A depende de B e B é rejeitado, A também é.
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, ModRecord*> live;
        for (ModRecord& m : mods)
            if (m.state == ModState::Discovered) live[m.manifest.id] = &m;
        for (auto& [id, m] : live) {
            for (const Dependency& d : m->manifest.dependencies) {
                auto it = live.find(d.id);
                if (it == live.end()) {
                    if (d.optional) continue;
                    reject(*m, "dependência \"" + d.id + "\" (" + d.req.Text() + ") ausente ou rejeitada");
                } else if (!d.req.Matches(it->second->manifest.version)) {
                    reject(*m, "dependência \"" + d.id + "\" precisa de " + d.req.Text() + ", encontrada " +
                                   it->second->manifest.version.ToString());
                } else {
                    continue;
                }
                changed = true;
                break;
            }
        }
    }

    // Ordenação topológica (Kahn). Empates resolvidos pelo id, para que a
    // ordem, e portanto os IDs de conteúdo, seja a mesma em toda máquina com
    // o mesmo conjunto de mods.
    std::map<std::string, ModRecord*> live;
    for (ModRecord& m : mods)
        if (m.state == ModState::Discovered) live[m.manifest.id] = &m;
    std::map<std::string, int> indegree;
    std::map<std::string, std::vector<std::string>> dependents;
    for (auto& [id, m] : live) {
        indegree[id];
        for (const Dependency& d : m->manifest.dependencies) {
            if (!live.count(d.id)) continue;  // opcional ausente
            ++indegree[id];
            dependents[d.id].push_back(id);
        }
    }
    std::priority_queue<std::string, std::vector<std::string>, std::greater<>> ready;
    for (auto& [id, deg] : indegree)
        if (deg == 0) ready.push(id);
    std::vector<std::string> order;
    while (!ready.empty()) {
        std::string id = ready.top();
        ready.pop();
        order.push_back(id);
        for (const std::string& dep : dependents[id])
            if (--indegree[dep] == 0) ready.push(dep);
    }
    for (auto& [id, deg] : indegree)
        if (deg > 0) reject(*live[id], "ciclo de dependências");
    return order;
}

size_t ModLoader::LoadAll() {
    if (loaded_) return 0;
    loaded_ = true;
    order_ = ResolveOrder(mods_);

    size_t loaded = 0;
    for (const std::string& id : order_) {
        auto it = std::find_if(mods_.begin(), mods_.end(),
                               [&](const ModRecord& m) { return m.manifest.id == id && m.state == ModState::Discovered; });
        if (it == mods_.end()) continue;
        ModRecord& rec = *it;
        size_t index = static_cast<size_t>(it - mods_.begin());

        // Dependência obrigatória que não carregou -> pula. Opcional que falhou
        // não impede: o mod deve funcionar sem ela.
        for (const Dependency& d : rec.manifest.dependencies) {
            const ModRecord* dep = Find(d.id);
            if (!d.optional && !(dep && dep->state == ModState::Loaded)) {
                rec.state = ModState::Skipped;
                rec.error = "dependência \"" + d.id + "\" não carregou";
                break;
            }
        }
        if (rec.state == ModState::Skipped) {
            Log(MODRIX_LOG_WARN, rec.manifest.id, rec.error);
            continue;
        }

        ModrixModInitFn init = rec.builtinInit;
        if (!init) {
            const char* platform = CurrentPlatformKey();
            auto entry = rec.manifest.entries.find(platform);
            if (entry == rec.manifest.entries.end()) {
                rec.state = ModState::Failed;
                rec.error = std::string("sem entry para a plataforma \"") + platform + "\"";
                Log(MODRIX_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
            std::string error;
            void* lib = dynlib::Open(rec.manifest.dir / entry->second, &error);
            if (!lib) {
                rec.state = ModState::Failed;
                rec.error = "não foi possível carregar " + entry->second + ": " + error;
                Log(MODRIX_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
            impl_->libraries.push_back(lib);
            init = reinterpret_cast<ModrixModInitFn>(dynlib::Symbol(lib, MODRIX_ENTRY_SYMBOL));
            if (!init) {
                rec.state = ModState::Failed;
                rec.error = std::string("símbolo ") + MODRIX_ENTRY_SYMBOL + " não exportado";
                Log(MODRIX_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
        }

        auto ctx = std::make_unique<ModrixContext>();
        ctx->loader = this;
        ctx->impl = impl_.get();
        ctx->index = index;
        ctx->version = rec.manifest.version.ToString();
        ctx->dir = rec.manifest.dir.string();

        auto checkpoint = registry_.Mark();
        size_t subsBefore = impl_->hurtSubs.size();
        size_t providersBefore = impl_->providers.size();
        ctx->initializing = true;
        int rc = init(&impl_->api, ctx.get());
        ctx->initializing = false;

        if (rc != 0) {
            registry_.Rollback(checkpoint);
            impl_->hurtSubs.resize(subsBefore);
            while (impl_->providers.size() > providersBefore) impl_->providers.pop_back();
            rec.state = ModState::Failed;
            rec.error = "modrix_mod_init retornou " + std::to_string(rc);
            Log(MODRIX_LOG_ERROR, rec.manifest.id, rec.error);
            impl_->contexts.push_back(std::move(ctx));  // ponteiro pode ter sido guardado pelo mod
            continue;
        }

        for (int k = 0; k < MODRIX_CONTENT_KIND_COUNT; ++k) {
            auto kind = static_cast<ModrixContentKind>(k);
            for (const std::string& n : rec.manifest.content[k]) {
                if (!registry_.Find(kind, rec.manifest.id + ":" + n))
                    Log(MODRIX_LOG_WARN, rec.manifest.id,
                        std::string("declarado em content.") + ContentKindKey(kind) + " mas não registrado: " + n);
            }
        }
        rec.state = ModState::Loaded;
        impl_->contexts.push_back(std::move(ctx));
        ++loaded;
        Log(MODRIX_LOG_INFO, rec.manifest.id, "carregado (" + rec.manifest.version.ToString() + ")");
    }
    return loaded;
}

}  // namespace modrix::rt
