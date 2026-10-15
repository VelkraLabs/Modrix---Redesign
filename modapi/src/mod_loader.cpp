#include "tlmod/runtime/mod_loader.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <mutex>
#include <queue>
#include <set>

#include "dynlib.h"
#include "json.h"
#include "tlmod/runtime/game_adapter.h"
#include "tlmod/runtime/log.h"

// Buffer em que um provedor escreve o JSON. Opaco do lado do mod.
struct TLModBuffer {
    std::string data;
};

// Contexto entregue ao mod. Opaco do lado do mod (tlmod_api.h só declara).
struct TLModContext {
    tlmod::rt::ModLoader* loader;
    tlmod::rt::ModLoader::Impl* impl;
    size_t index;             // em ModLoader::Mods()
    bool initializing = false;
    std::string version;      // cache para mod_version()
    std::string dir;          // cache para mod_dir()
};

namespace tlmod::rt {

const char* ToString(ModState s) {
    switch (s) {
        case ModState::Discovered: return "descoberto";
        case ModState::Rejected: return "rejeitado";
        case ModState::Loaded: return "carregado";
        case ModState::Failed: return "falhou";
        case ModState::Skipped: return "pulado";
        case ModState::Disabled: return "desativado";
    }
    return "?";
}

struct ModLoader::Impl {
    struct HurtSub {
        TLModContext* ctx;
        TLModPlayerHurtFn fn;
        void* user;
    };

    ModLoader* self;
    GameAdapter* game;
    TLModHostApi api{};
    std::vector<std::unique_ptr<TLModContext>> contexts;
    std::vector<void*> libraries;  // nunca fechadas: hooks/eventos podem apontar para elas
    std::vector<HurtSub> hurtSubs;

    // Provedores de dados para a interface ("mod_id/channel").
    struct Provider {
        std::string key;
        TLModProviderFn fn;
        void* user;
        std::unique_ptr<std::mutex> mutex;  // serializa chamadas ao mesmo provedor
    };
    std::vector<Provider> providers;

    const ModRecord& Mod(const TLModContext* ctx) const { return self->mods_[ctx->index]; }
    const std::string& ModId(const TLModContext* ctx) const { return Mod(ctx).manifest.id; }

    bool DispatchHurt(TLModPlayerHurt& ev, bool after) {
        for (const HurtSub& s : hurtSubs) {
            bool keep = s.fn(s.user, &ev, after);
            if (!after && !keep) return false;  // um mod cancelou o golpe
        }
        return true;
    }

    // ---- NPCs de mod ----------------------------------------------------------
    // Tabela lateral: para cada slot de Main.npc, qual NPC de mod está ali
    // (TLMOD_INVALID_ID = NPC original) e o estado privado local[] dele.
    // É o "registro de spawns": preenchido no pós-NewNPC, limpo no SetDefaults.
    struct NpcSlot {
        TLModId type = TLMOD_INVALID_ID;
        float local[8] = {};
        uint32_t ticks = 0;  // ticks de AI desde o spawn (animação automática)
        int32_t frame = -1;  // TLModNpcState.frame: -1 = automático
    };
    std::vector<NpcSlot> npcSlots;
    uint64_t modNpcSpawns = 0;

    // ---- Itens de mod -----------------------------------------------------------
    const ItemDefinition* ModItemDef(int32_t type) const {
        const ContentEntry* e = self->registry_.Get(TLMOD_CONTENT_ITEM, type);
        return e ? e->Item() : nullptr;
    }
    // Pré-Item.SetDefaults: ID de mod -> casca original. O ID de mod nunca chega às tabelas.
    int32_t RemapItemType(int32_t type) const {
        const ItemDefinition* def = ModItemDef(type);
        return def ? def->shellType : type;
    }

    // give_item: pedidos de qualquer thread, entregues na thread do jogo.
    struct Give {
        std::string modId;
        TLModId type;
        int32_t stack;
    };
    std::mutex giveMutex;
    std::vector<Give> pendingGives;

    // ---- Texturas de mod -------------------------------------------------------
    // PNGs decodificados no LoadAll (qualquer thread) e enviados ao motor no
    // frame handler (thread principal do Unity), poucos por frame. Os mapas
    // tipo -> id são montados no LoadAll, antes de os ganchos existirem; depois
    // só os valores (atômicos) mudam, então o desenho lê sem trava.
    struct TextureSlot {
        std::atomic<int32_t> id{0};
    };
    std::map<int32_t, TextureSlot> itemTextures;  // ID do item de mod -> textura
    std::map<int32_t, TextureSlot> npcTextures;   // ID do NPC de mod -> textura
    struct PendingTexture {
        std::string modId;
        std::string what;  // "mod:nome (arquivo)" para o log
        Image image;
        TextureSlot* slot;
    };
    std::deque<PendingTexture> pendingTextures;  // só a thread principal mexe depois do LoadAll
    static constexpr int kTexturesPerFrame = 4;
    int32_t texturesCreated = 0;

    void ProcessTextures() {
        for (int n = 0; n < kTexturesPerFrame && !pendingTextures.empty(); ++n) {
            PendingTexture& t = pendingTextures.front();
            int32_t id = game->CreateTexture(t.image, t.what);
            if (id < 0) return;  // o jogo ainda não carregou as texturas: próximo frame
            if (id == 0) {
                Log(TLMOD_LOG_ERROR, t.modId, "textura " + t.what + ": o jogo recusou; fica a da casca");
            } else {
                t.slot->id.store(id, std::memory_order_release);
                ++texturesCreated;
                Log(TLMOD_LOG_DEBUG, t.modId, "textura " + t.what + " -> id " + std::to_string(id));
            }
            pendingTextures.pop_front();
        }
    }

    int32_t ItemTexture(int32_t type) const {
        auto it = itemTextures.find(type);
        return it == itemTextures.end() ? 0 : it->second.id.load(std::memory_order_acquire);
    }

    bool NpcTexture(int32_t index, GameAdapter::NpcDraw& out) const {
        if (!ValidSlot(index)) return false;
        const NpcSlot& slot = npcSlots[static_cast<size_t>(index)];
        if (slot.type == TLMOD_INVALID_ID) return false;
        auto it = npcTextures.find(slot.type);
        if (it == npcTextures.end()) return false;
        int32_t id = it->second.id.load(std::memory_order_acquire);
        const NpcDefinition* def = ModNpcDef(slot.type);
        if (id <= 0 || !def) return false;
        out.texture = id;
        out.frameCount = def->frameCount;
        if (slot.frame >= 0)
            out.frame = slot.frame % def->frameCount;
        else
            out.frame = static_cast<int32_t>((slot.ticks / static_cast<uint32_t>(def->frameTicks)) %
                                             static_cast<uint32_t>(def->frameCount));
        return true;
    }

    void OnFrame() {
        ProcessTextures();
        std::vector<Give> todo;
        {
            std::lock_guard<std::mutex> lock(giveMutex);
            if (pendingGives.empty()) return;
            // Só com um mundo carregado: no menu não há inventário para encher.
            if (game->InWorld() != std::optional<bool>(true)) return;
            todo.swap(pendingGives);
        }
        for (const Give& g : todo) {
            int32_t given = game->GiveItem(g.type, g.stack);
            const ContentEntry* e = self->registry_.Get(TLMOD_CONTENT_ITEM, g.type);
            std::string what = e ? e->key : "item " + std::to_string(g.type);
            if (given == g.stack)
                Log(TLMOD_LOG_INFO, g.modId, "give_item: " + std::to_string(given) + "x " + what + " entregue");
            else
                Log(TLMOD_LOG_WARN, g.modId,
                    "give_item: só " + std::to_string(given) + " de " + std::to_string(g.stack) + "x " + what +
                        " couberam no inventário");
        }
    }

    const NpcDefinition* ModNpcDef(int32_t type) const {
        const ContentEntry* e = self->registry_.Get(TLMOD_CONTENT_NPC, type);
        return e ? e->Npc() : nullptr;
    }
    bool ValidSlot(int32_t index) const {
        return index >= 0 && static_cast<size_t>(index) < npcSlots.size();
    }
    bool ReadModNpc(int32_t index, TLModNpcState& st) const {
        if (!game->ReadNpc(index, st)) return false;
        st.index = index;
        const NpcSlot& slot = npcSlots[static_cast<size_t>(index)];
        st.type = slot.type;  // o jogo só vê a casca
        std::copy(std::begin(slot.local), std::end(slot.local), st.local);
        st.frame = slot.frame;
        return true;
    }
    void WriteModNpc(int32_t index, const TLModNpcState& st) {
        game->WriteNpc(index, st);
        NpcSlot& slot = npcSlots[static_cast<size_t>(index)];
        std::copy(std::begin(st.local), std::end(st.local), slot.local);
        slot.frame = st.frame < 0 ? -1 : st.frame;
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
        const ContentEntry* e = self->registry_.Get(TLMOD_CONTENT_NPC, requested);
        if (!ValidSlot(index)) {
            Log(TLMOD_LOG_WARN, e->modId, "spawn de " + e->key + " falhou: sem slot livre");
            return;
        }
        npcSlots[static_cast<size_t>(index)] = NpcSlot{};
        npcSlots[static_cast<size_t>(index)].type = requested;
        ++modNpcSpawns;
        Log(TLMOD_LOG_DEBUG, e->modId,
            "spawn " + e->key + " no slot " + std::to_string(index) + " (casca " + std::to_string(spawned) + ")");

        TLModNpcState st{};
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
        NpcSlot& slot = npcSlots[static_cast<size_t>(index)];
        if (slot.type == TLMOD_INVALID_ID) return false;
        ++slot.ticks;  // relógio da animação automática, mesmo sem on_ai
        const NpcDefinition* def = ModNpcDef(slot.type);
        if (!def || !def->onAi) return false;
        TLModNpcState st{};
        if (!ReadModNpc(index, st)) return false;
        bool replace = def->onAi(def->user, &st);
        WriteModNpc(index, st);
        return replace;
    }
};

namespace {

using Impl = ModLoader::Impl;

Impl* ImplOf(const TLModContext* ctx) { return ctx ? ctx->impl : nullptr; }

// ---- Implementação de TLModHostApi -------------------------------------
// Todas validam o contexto; nenhum erro de mod derruba o runtime.

const char* Api_ModId(const TLModContext* ctx) {
    return ctx ? ImplOf(ctx)->ModId(ctx).c_str() : "";
}
const char* Api_ModVersion(const TLModContext* ctx) { return ctx ? ctx->version.c_str() : ""; }
const char* Api_ModDir(const TLModContext* ctx) { return ctx ? ctx->dir.c_str() : ""; }

void Api_Log(const TLModContext* ctx, TLModLogLevel level, const char* msg) {
    Log(level, ctx ? ImplOf(ctx)->ModId(ctx) : "?", msg ? msg : "");
}

constexpr uint32_t kItemDefV1Size = static_cast<uint32_t>(offsetof(TLModItemDef, use_time) + sizeof(int32_t));

TLModId Api_RegisterItem(TLModContext* ctx, const TLModItemDef* def) {
    if (!ctx) return TLMOD_INVALID_ID;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(TLMOD_LOG_ERROR, mod.manifest.id, "register_item: " + msg);
        return TLMOD_INVALID_ID;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante tlmod_mod_init");
    if (!def || def->struct_size < kItemDefV1Size) return err("TLModItemDef nulo ou struct_size inválido");
    if (!def->name) return err("name é obrigatório");
    std::string name = def->name;
    if (!mod.manifest.Declares(TLMOD_CONTENT_ITEM, name))
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
    // shell_type veio depois: só existe se struct_size o cobre.
    constexpr size_t kShellEnd = offsetof(TLModItemDef, shell_type) + sizeof(TLModId);
    TLModId shell = def->struct_size >= kShellEnd ? def->shell_type : 0;
    int32_t vanilla = impl->self->Registry().VanillaCount(TLMOD_CONTENT_ITEM).value_or(0);
    if (shell == 0) shell = kDefaultItemShell;
    if (shell < 1 || shell >= vanilla)
        return err("shell_type " + std::to_string(shell) + " não é um item original (1.." +
                   std::to_string(vanilla - 1) + ")");
    item.shellType = shell;

    std::string error;
    auto id = impl->self->Registry().Register(TLMOD_CONTENT_ITEM, mod.manifest.id, name,
                                              std::move(item), &error);
    if (!id) return err(error);
    Log(TLMOD_LOG_INFO, mod.manifest.id,
        "item " + mod.manifest.id + ":" + name + " -> ID " + std::to_string(*id));
    return *id;
}

TLModId Api_FindContent(const TLModContext* ctx, TLModContentKind kind, const char* key) {
    if (!ctx || !key) return TLMOD_INVALID_ID;
    auto id = ImplOf(ctx)->self->Registry().Find(kind, key);
    return id ? *id : TLMOD_INVALID_ID;
}

bool Api_SubscribePlayerHurt(TLModContext* ctx, TLModPlayerHurtFn fn, void* user) {
    if (!ctx || !fn) return false;
    Impl* impl = ImplOf(ctx);
    if (!ctx->initializing) {
        Log(TLMOD_LOG_ERROR, impl->ModId(ctx), "subscribe_player_hurt: só durante tlmod_mod_init");
        return false;
    }
    impl->hurtSubs.push_back({ctx, fn, user});
    return true;
}

TLModPlayer Api_LocalPlayer(const TLModContext* ctx) {
    return ctx ? ImplOf(ctx)->game->LocalPlayer() : TLMOD_NO_PLAYER;
}

int32_t Api_PlayerLife(const TLModContext* ctx, TLModPlayer p) {
    return ctx ? ImplOf(ctx)->game->PlayerLife(p) : -1;
}

constexpr uint32_t kNpcDefV1Size = static_cast<uint32_t>(offsetof(TLModNpcDef, on_ai) + sizeof(TLModNpcAiFn));

TLModId Api_RegisterNpc(TLModContext* ctx, const TLModNpcDef* def) {
    if (!ctx) return TLMOD_INVALID_ID;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(TLMOD_LOG_ERROR, mod.manifest.id, "register_npc: " + msg);
        return TLMOD_INVALID_ID;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante tlmod_mod_init");
    if (!def || def->struct_size < kNpcDefV1Size) return err("TLModNpcDef nulo ou struct_size inválido");
    if (!def->name) return err("name é obrigatório");
    std::string name = def->name;
    if (!mod.manifest.Declares(TLMOD_CONTENT_NPC, name))
        return err("\"" + name + "\" não está declarado em content.npcs do manifest.json");
    if (!def->display_name || !*def->display_name) return err("display_name é obrigatório");

    // A casca tem de ser um NPC original existente: é ela que o jogo vê.
    ContentRegistry& reg = impl->self->Registry();
    int32_t vanilla = reg.VanillaCount(TLMOD_CONTENT_NPC).value_or(0);
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
    // texture/frame_count/frame_ticks vieram depois: só se struct_size os cobre.
    constexpr size_t kTextureEnd = offsetof(TLModNpcDef, frame_ticks) + sizeof(int32_t);
    if (def->struct_size >= kTextureEnd && def->texture) {
        if (!IsSafeRelativePath(def->texture))
            return err("texture deve ser caminho relativo dentro da pasta do mod");
        if (def->frame_count < 1 || def->frame_count > 256) return err("frame_count deve estar em 1..256");
        npc.texture = (mod.manifest.dir / def->texture).lexically_normal().string();
        npc.frameCount = def->frame_count;
        npc.frameTicks = def->frame_ticks > 0 ? def->frame_ticks : 8;
    }

    std::string error;
    auto id = reg.Register(TLMOD_CONTENT_NPC, mod.manifest.id, name, std::move(npc), &error);
    if (!id) return err(error);
    Log(TLMOD_LOG_INFO, mod.manifest.id,
        "npc " + mod.manifest.id + ":" + name + " -> ID " + std::to_string(*id) + " (casca " +
            std::to_string(def->shell_type) + ")");
    return *id;
}

int32_t ToTri(std::optional<bool> v) { return v ? (*v ? 1 : 0) : -1; }

int32_t Api_InWorld(const TLModContext* ctx) { return ctx ? ToTri(ImplOf(ctx)->game->InWorld()) : -1; }

int32_t Api_Progress(const TLModContext* ctx, TLModProgress flag) {
    if (!ctx || flag < 0 || flag >= TLMOD_PROGRESS_COUNT) return -1;
    GameAdapter* game = ImplOf(ctx)->game;
    // Flags só fazem sentido com um mundo carregado; no menu ficam com o
    // valor do último mundo (ou lixo), então a resposta é "desconhecido".
    if (game->InWorld() != std::optional<bool>(true)) return -1;
    return ToTri(game->Progress(flag));
}

bool Api_RegisterProvider(TLModContext* ctx, const char* channel, TLModProviderFn fn, void* user) {
    if (!ctx) return false;
    Impl* impl = ImplOf(ctx);
    const ModRecord& mod = impl->Mod(ctx);
    auto err = [&](const std::string& msg) {
        Log(TLMOD_LOG_ERROR, mod.manifest.id, "register_provider: " + msg);
        return false;
    };
    if (!ctx->initializing) return err("só pode ser chamado durante tlmod_mod_init");
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

void Api_BufferAppend(TLModBuffer* out, const char* data, size_t len) {
    if (out && data) out->data.append(data, len);
}

bool Api_GiveItem(TLModContext* ctx, TLModId type, int32_t stack) {
    if (!ctx) return false;
    Impl* impl = ImplOf(ctx);
    const ContentRegistry& reg = impl->self->Registry();
    int32_t vanilla = reg.VanillaCount(TLMOD_CONTENT_ITEM).value_or(0);
    if (!reg.IsModId(TLMOD_CONTENT_ITEM, type) && !(type > 0 && type < vanilla)) {
        Log(TLMOD_LOG_ERROR, impl->ModId(ctx), "give_item: tipo " + std::to_string(type) + " desconhecido");
        return false;
    }
    if (stack < 1) {
        Log(TLMOD_LOG_ERROR, impl->ModId(ctx), "give_item: stack precisa ser >= 1");
        return false;
    }
    std::lock_guard<std::mutex> lock(impl->giveMutex);
    impl->pendingGives.push_back({impl->ModId(ctx), type, stack});
    return true;
}

int32_t Api_SpawnNpc(TLModContext* ctx, TLModId type, float x, float y) {
    if (!ctx) return -1;
    Impl* impl = ImplOf(ctx);
    const ContentRegistry& reg = impl->self->Registry();
    int32_t vanilla = reg.VanillaCount(TLMOD_CONTENT_NPC).value_or(0);
    bool known = reg.IsModId(TLMOD_CONTENT_NPC, type) || (type > 0 && type < vanilla);
    if (!known) {
        Log(TLMOD_LOG_ERROR, impl->ModId(ctx), "spawn_npc: tipo " + std::to_string(type) + " desconhecido");
        return -1;
    }
    // Passa pelo NewNPC do jogo; o remap para a casca acontece no gancho.
    return impl->game->SpawnNpc(type, x, y);
}

}  // namespace

// ---- ModLoader -----------------------------------------------------------

ModLoader::ModLoader(GameAdapter* game)
    : game_(game),
      registry_(game),
      impl_(std::make_unique<Impl>()),
      persistence_(std::make_unique<ItemPersistence>(registry_)) {
    impl_->self = this;
    impl_->game = game;
    TLModHostApi& a = impl_->api;
    a.api_version = TLMOD_API_VERSION;
    a.struct_size = sizeof(TLModHostApi);
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
    a.give_item = &Api_GiveItem;

    Impl* impl = impl_.get();
    game_->SetPlayerHurtHandler([impl](TLModPlayerHurt& ev, bool after) { return impl->DispatchHurt(ev, after); });

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
    game_->SetPersistenceHooks({});
    game_->SetItemHooks({});
    game_->SetFrameHandler(nullptr);
    game_->SetDrawHooks({});
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
        TLModBuffer buf;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(*p.mutex);
            ok = p.fn(p.user, &buf);
        }
        if (!ok) return fail(key + ": o provedor devolveu erro");
        // A interface recebe sempre JSON válido: um mod com bug não a quebra.
        std::string jsonError;
        if (!json::Parse(buf.data, &jsonError)) {
            Log(TLMOD_LOG_ERROR, modId, "provedor " + channel + " gerou JSON inválido: " + jsonError);
            return fail(key + ": " + jsonError);
        }
        return std::move(buf.data);
    }
    return fail("canal " + key + " não existe");
}

TLModId ModLoader::NpcInSlot(int32_t index) const {
    return impl_->ValidSlot(index) ? impl_->npcSlots[static_cast<size_t>(index)].type : TLMOD_INVALID_ID;
}

const TLModHostApi& ModLoader::HostApi() const { return impl_->api; }

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
    if (ec) Log(TLMOD_LOG_WARN, "loader", "não foi possível listar " + modsDir.string() + ": " + ec.message());
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

void ModLoader::AddBuiltin(ModManifest manifest, TLModModInitFn init) {
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
        if (m.state == ModState::Discovered && m.manifest.apiVersion != TLMOD_API_VERSION)
            reject(m, "tlmod_api " + std::to_string(m.manifest.apiVersion) + " incompatível (runtime: " +
                          std::to_string(TLMOD_API_VERSION) + ")");
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

void ModLoader::LoadTextures() {
    auto load = [&](const ContentEntry& e, const std::string& file, int32_t frameCount,
                    std::map<int32_t, Impl::TextureSlot>& slots) {
        std::string error;
        std::optional<Image> img = LoadPng(file, &error);
        std::string what = e.key + " (" + std::filesystem::path(file).filename().string() + ")";
        if (!img) {
            Log(TLMOD_LOG_ERROR, e.modId, "textura de " + e.key + ": " + error + "; fica a da casca");
            return;
        }
        if (img->height % frameCount != 0) {
            Log(TLMOD_LOG_ERROR, e.modId,
                "textura de " + e.key + ": altura " + std::to_string(img->height) + " não é múltipla de " +
                    std::to_string(frameCount) + " quadros; fica a da casca");
            return;
        }
        Impl::TextureSlot& slot = slots[e.id];
        impl_->pendingTextures.push_back({e.modId, what, std::move(*img), &slot});
    };
    for (const ContentEntry& e : registry_.Entries(TLMOD_CONTENT_ITEM))
        if (const ItemDefinition* d = e.Item(); d && !d->texture.empty()) load(e, d->texture, 1, impl_->itemTextures);
    for (const ContentEntry& e : registry_.Entries(TLMOD_CONTENT_NPC))
        if (const NpcDefinition* d = e.Npc(); d && !d->texture.empty())
            load(e, d->texture, d->frameCount, impl_->npcTextures);
}

size_t ModLoader::PendingTextures() const { return impl_->pendingTextures.size(); }
int32_t ModLoader::TexturesCreated() const { return impl_->texturesCreated; }

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

        if (disabled_.count(rec.manifest.id)) {
            rec.state = ModState::Disabled;
            rec.error = "desativado nas configurações";
            Log(TLMOD_LOG_INFO, rec.manifest.id, rec.error);
            continue;
        }

        // Dependência obrigatória que não carregou -> pula. Opcional que falhou
        // não impede: o mod deve funcionar sem ela.
        for (const Dependency& d : rec.manifest.dependencies) {
            const ModRecord* dep = Find(d.id);
            if (!d.optional && !(dep && dep->state == ModState::Loaded)) {
                rec.state = ModState::Skipped;
                rec.error = dep && dep->state == ModState::Disabled
                                ? "dependência \"" + d.id + "\" está desativada"
                                : "dependência \"" + d.id + "\" não carregou";
                break;
            }
        }
        if (rec.state == ModState::Skipped) {
            Log(TLMOD_LOG_WARN, rec.manifest.id, rec.error);
            continue;
        }

        TLModModInitFn init = rec.builtinInit;
        if (!init) {
            const char* platform = CurrentPlatformKey();
            auto entry = rec.manifest.entries.find(platform);
            if (entry == rec.manifest.entries.end()) {
                rec.state = ModState::Failed;
                rec.error = std::string("sem entry para a plataforma \"") + platform + "\"";
                Log(TLMOD_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
            std::string error;
            void* lib = dynlib::Open(rec.manifest.dir / entry->second, &error);
            if (!lib) {
                rec.state = ModState::Failed;
                rec.error = "não foi possível carregar " + entry->second + ": " + error;
                Log(TLMOD_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
            impl_->libraries.push_back(lib);
            init = reinterpret_cast<TLModModInitFn>(dynlib::Symbol(lib, TLMOD_ENTRY_SYMBOL));
            if (!init) {
                rec.state = ModState::Failed;
                rec.error = std::string("símbolo ") + TLMOD_ENTRY_SYMBOL + " não exportado";
                Log(TLMOD_LOG_ERROR, rec.manifest.id, rec.error);
                continue;
            }
        }

        auto ctx = std::make_unique<TLModContext>();
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
            rec.error = "tlmod_mod_init retornou " + std::to_string(rc);
            Log(TLMOD_LOG_ERROR, rec.manifest.id, rec.error);
            impl_->contexts.push_back(std::move(ctx));  // ponteiro pode ter sido guardado pelo mod
            continue;
        }

        for (int k = 0; k < TLMOD_CONTENT_KIND_COUNT; ++k) {
            auto kind = static_cast<TLModContentKind>(k);
            for (const std::string& n : rec.manifest.content[k]) {
                if (!registry_.Find(kind, rec.manifest.id + ":" + n))
                    Log(TLMOD_LOG_WARN, rec.manifest.id,
                        std::string("declarado em content.") + ContentKindKey(kind) + " mas não registrado: " + n);
            }
        }
        rec.state = ModState::Loaded;
        impl_->contexts.push_back(std::move(ctx));
        ++loaded;
        Log(TLMOD_LOG_INFO, rec.manifest.id, "carregado (" + rec.manifest.version.ToString() + ")");
    }

    // Itens de mod só agora, com o registro completo (o jogo chama SetDefaults
    // em qualquer thread: o registro não pode estar mudando).
    Impl* impl = impl_.get();
    GameAdapter::ItemHooks items;
    items.remapType = [impl](int32_t t) { return impl->RemapItemType(t); };
    items.definition = [impl](int32_t t) { return impl->ModItemDef(t); };
    game_->SetItemHooks(std::move(items));

    // Texturas: lê e decodifica os PNGs agora; o envio ao motor fica na fila do
    // frame handler (thread principal). Um PNG ruim só custa a textura: o
    // conteúdo continua com a da casca.
    LoadTextures();
    GameAdapter::DrawHooks draw;
    draw.itemTexture = [impl](int32_t t) { return impl->ItemTexture(t); };
    draw.npcTexture = [impl](int32_t i, GameAdapter::NpcDraw& out) { return impl->NpcTexture(i, out); };
    game_->SetDrawHooks(std::move(draw));
    game_->SetFrameHandler([impl] { impl->OnFrame(); });

    // Save/load só agora, com o registro completo: antes disso uma chave de
    // mod ainda não registrado viraria órfã à toa. Liga mesmo sem itens de mod
    // (órfãos de mods removidos continuam sendo regravados).
    ItemPersistence* p = persistence_.get();
    GameAdapter::PersistenceHooks hooks;
    hooks.beforeSave = [p](const SaveFile& f, ItemSlots& s) { p->BeforeSave(f, s); };
    hooks.afterSave = [p](const SaveFile& f, ItemSlots& s, bool ok) { p->AfterSave(f, s, ok); };
    hooks.afterLoad = [p](const SaveFile& f, ItemSlots& s) { p->AfterLoad(f, s); };
    game_->SetPersistenceHooks(std::move(hooks));
    return loaded;
}

}  // namespace tlmod::rt
