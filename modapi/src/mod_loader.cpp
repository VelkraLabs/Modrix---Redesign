#include "modrix/runtime/mod_loader.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <queue>
#include <set>

#include "dynlib.h"
#include "modrix/runtime/game_adapter.h"
#include "modrix/runtime/log.h"

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

    const ModRecord& Mod(const ModrixContext* ctx) const { return self->mods_[ctx->index]; }
    const std::string& ModId(const ModrixContext* ctx) const { return Mod(ctx).manifest.id; }

    bool DispatchHurt(ModrixPlayerHurt& ev, bool after) {
        for (const HurtSub& s : hurtSubs) {
            bool keep = s.fn(s.user, &ev, after);
            if (!after && !keep) return false;  // um mod cancelou o golpe
        }
        return true;
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

    game_->SetPlayerHurtHandler(
        [impl = impl_.get()](ModrixPlayerHurt& ev, bool after) { return impl->DispatchHurt(ev, after); });
}

ModLoader::~ModLoader() { game_->SetPlayerHurtHandler(nullptr); }

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
        ctx->initializing = true;
        int rc = init(&impl_->api, ctx.get());
        ctx->initializing = false;

        if (rc != 0) {
            registry_.Rollback(checkpoint);
            impl_->hurtSubs.resize(subsBefore);
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
