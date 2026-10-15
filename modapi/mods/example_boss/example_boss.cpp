// Esqueleto de boss customizado. Mostra ONDE cada parte entra; não tem
// lógica de combate ainda.
//
// Como o boss chega ao jogo (camadas abaixo do mod, já implementadas):
//
//   host.SpawnNpc(id, x, y)  ou  qualquer NPC.NewNPC(Type = id)
//     └ hook em NPC.NewNPC (RVA 0xEE4630)
//         pré:  id 4096+ vira a casca (shell_type) — o jogo nunca vê o ID de mod
//         pós:  o runtime registra "slot i = ember_titan" e chama OnSpawn
//   a cada tick: Main.DoUpdateInWorld -> NPC.UpdateNPC(i) (0xF4A2E8)
//     └ hook em NPC.AI (RVA 0xDCE54C): o runtime chama OnAI deste mod
//   NPC.SetDefaults (0xDAF1BC) no slot (morte e reuso, Transform, rede)
//     └ o runtime desmarca o slot: o comportamento deste mod para ali
#include <modrix/modrix.hpp>

#include <string>

namespace {

// Estado do boss em ModrixNpcState::local[]: privado do mod, o jogo não
// toca. ai[0..3] ficam para a AI da casca nas fases em que ela roda.
enum Local { kPhase = 0, kTimer = 1 };
enum Phase { kIntro = 0, kPhase1 = 1, kEnraged = 2 };

constexpr float kIntroTicks = 120.0f;  // 2 s a 60 ticks/s

class EmberTitan final : public modrix::NpcBehavior {
public:
    // Logo depois de o NPC entrar no mundo. life/damage/defense da
    // definição (NpcDef) já foram aplicados pelo runtime.
    void OnSpawn(ModrixNpcState& npc) override {
        npc.local[kPhase] = kIntro;
        npc.local[kTimer] = 0.0f;
        // TODO: texto "O Titã de Brasa despertou!", música de boss, etc.
    }

    // Uma vez por tick, antes da AI da casca.
    //   true  -> este código assume o tick (a AI da casca não roda)
    //   false -> a AI da casca roda em seguida (aqui, a do Olho de Cthulhu)
    // Velocidade escrita aqui é aplicada pelo jogo logo depois, em UpdateNPC.
    bool OnAI(ModrixNpcState& npc) override {
        npc.local[kTimer] += 1.0f;
        switch (static_cast<int>(npc.local[kPhase])) {
            case kIntro:
                // TODO: animação de entrada. Por enquanto: fica parado.
                npc.vx = npc.vy = 0.0f;
                if (npc.local[kTimer] >= kIntroTicks) Enter(npc, kPhase1);
                return true;

            case kPhase1:
                // TODO: padrão de ataque próprio (projéteis, investidas...).
                // Até lá, delega à AI da casca.
                if (npc.life * 2 < npc.life_max) Enter(npc, kEnraged);
                return false;

            case kEnraged:
                // TODO: ataques da segunda fase.
                return false;
        }
        return false;
    }

private:
    static void Enter(ModrixNpcState& npc, Phase p) {
        npc.local[kPhase] = static_cast<float>(p);
        npc.local[kTimer] = 0.0f;
    }
};

EmberTitan g_emberTitan;  // vive enquanto o jogo roda

}  // namespace

static bool OnLoad(modrix::Host& host) {
    modrix::NpcDef def;
    def.name = "ember_titan";      // declarado em content.npcs
    def.displayName = "Titã de Brasa";
    def.shellType = 4;             // NPCID 4: Olho de Cthulhu (textura, hitbox, AI de fallback)
    def.lifeMax = 12000;
    def.damage = 40;
    def.defense = 14;
    def.boss = true;

    ModrixId id = host.RegisterNpc(def, &g_emberTitan);
    if (id == MODRIX_INVALID_ID) return false;
    host.Info("Titã de Brasa registrado com ID " + std::to_string(id));

    // TODO: invocação. Um item de mod (host.RegisterItem) cujo uso chame
    // host.SpawnNpc(id, x, y) perto do player.
    return true;
}

MODRIX_MOD(OnLoad)
