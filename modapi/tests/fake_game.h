// GameAdapter falso para testes: simula players e o fluxo de Player.Hurt.
#pragma once

#include <array>
#include <cstring>

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
};
