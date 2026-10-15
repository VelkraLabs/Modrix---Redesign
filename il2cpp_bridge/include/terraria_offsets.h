// Offsets e RVAs do Terraria Mobile extraídos do dump do Il2CppDumper.
//
// Build analisada:
//   libil2cpp.so        arm64-v8a, 48.408.592 bytes
//                       sha256 3c45ab44d50e54b3769d906655b2b6e637df39e6a400447a57996d32e05fc228
//   global-metadata.dat v31, sha256 957c596a09795d6a8cf1d8e0f07d44375cf5301c2d14d63ea75c64a600c55f2a
//   Unity               2021.3.56f2
//
// A ponte NÃO depende destes valores para funcionar: ela resolve tudo pelo nome
// via API do IL2CPP (il2cpp_field_get_offset, MethodInfo::methodPointer). Os
// valores abaixo servem como (a) checagem de que a build é a mesma do dump e
// (b) fallback documentado. Se outra versão do jogo for usada, a checagem
// acusa a divergência no log e os valores resolvidos em runtime prevalecem.
#pragma once

#include <cstddef>
#include <cstdint>

namespace terraria {

// --- Assembly / classes ---------------------------------------------------
constexpr const char* kAssembly = "Assembly-CSharp.dll";
constexpr const char* kNamespace = "Terraria";
constexpr const char* kPlayerClass = "Player";  // TypeDefIndex 1046 (dump.cs)
constexpr const char* kMainClass = "Main";      // TypeDefIndex 1017 (dump.cs)
constexpr const char* kNpcClass = "NPC";        // TypeDefIndex 1022 (dump.cs)

// Main.maxNPCs (const = 200): tamanho de Main.npc[]. NewNPC devolve esse
// valor quando não há slot livre.
constexpr int32_t kMaxNPCs = 200;
// NPCID.Count (const = 688). IDs negativos (-65..-1) também são originais.
constexpr int32_t kNpcVanillaCount = 688;

// --- Campos de instância de Terraria.Player --------------------------------
// Offsets relativos ao ponteiro Player* (incluem o cabeçalho de 0x10 bytes).
// Fonte: dump.cs / player_fields.md.
namespace player_off {
constexpr size_t whoAmI = 0x10;        // int   (herdado de Entity)
constexpr size_t active = 0x14;        // bool  (herdado de Entity)
constexpr size_t position = 0x20;      // Vector2 (herdado de Entity)
constexpr size_t immune = 0x3A8;       // bool
constexpr size_t immuneTime = 0x3AC;   // int
constexpr size_t dead = 0x534;         // bool
constexpr size_t statDefense = 0x6F8;  // int
constexpr size_t statLifeMax = 0x6FC;  // int
constexpr size_t statLifeMax2 = 0x700; // int  (vida máxima com bônus)
constexpr size_t statLife = 0x704;     // int  <-- usado pela ponte
}  // namespace player_off

// --- Campos de instância de Terraria.NPC -----------------------------------
// Os de Entity são os mesmos de Player (whoAmI 0x10, active 0x14, ...).
namespace npc_off {
constexpr size_t whoAmI = 0x10;     // int   (Entity)
constexpr size_t active = 0x14;     // bool  (Entity)
constexpr size_t position = 0x20;   // Vector2 (Entity)
constexpr size_t velocity = 0x28;   // Vector2 (Entity)
constexpr size_t type = 0x154;      // int
constexpr size_t ai = 0x158;        // float[] (4 posições; ai[0..3] guardam o estado da IA)
constexpr size_t target = 0x178;    // int   (índice do player alvo)
constexpr size_t damage = 0x17C;    // int
constexpr size_t defense = 0x180;   // int
constexpr size_t life = 0x1A0;      // int
constexpr size_t lifeMax = 0x1A4;   // int
}  // namespace npc_off

// --- Campos estáticos de Terraria.Main -------------------------------------
// Offset dentro de static_fields da classe; a ponte usa
// il2cpp_field_static_get_value e não este número diretamente.
namespace main_static_off {
constexpr size_t npc = 0x5B0;      // NPC[] Main.npc (200 slots)
constexpr size_t player = 0x678;   // Player[] Main.player (255 slots)
constexpr size_t netMode = 0x81C;  // int Main.netMode
}  // namespace main_static_off

// --- Estado do mundo -------------------------------------------------------
// LocalUserGameState (namespace global, TypeDefIndex 695):
//   [ThreadStatic] static LocalUserGameState Instance
//   bool gameMenu  @0x320   (true = menu, false = dentro de um mundo)
constexpr const char* kLocalUserGameStateClass = "LocalUserGameState";
namespace local_user_off {
constexpr size_t gameMenu = 0x320;
}
// Flags de progresso: static bool em Terraria.NPC (static_fields+offset), lidas
// por nome via il2cpp_field_static_get_value. Offsets do dump, só referência:
//   downedBoss1 0x8E (Olho de Cthulhu)  downedBoss2 0x8F (Devorador/Cérebro)
//   downedBoss3 0x90 (Esqueletron)      downedQueenBee 0x91  downedSlimeKing 0x92
//   downedPlantBoss 0x97  downedGolemBoss 0x98  downedFishron 0x9A
//   downedAncientCultist 0xA0  downedMoonlord 0xA1  downedEmpressOfLight 0xA6
//   downedQueenSlime 0xA7  downedDeerclops 0xA8  downedMechBoss1..3 0xCA..0xCC
// Main.hardMode (static_fields+0x360) liga quando o Wall of Flesh morre.

// --- RVAs de métodos (endereço real = base da libil2cpp.so + RVA) ----------
namespace rva {
// double Player.Hurt(PlayerDeathReason damageSource, int Damage, int hitDirection,
//                    bool pvp = false, bool quiet = false, bool Crit = false,
//                    int cooldownCounter = -1, bool dodgeable = true)
// Único overload de Hurt na classe.
constexpr uintptr_t Player_Hurt = 0xFA15B8;
constexpr uintptr_t Player_Heal = 0xFF704C;    // void Player.Heal(int amount)
constexpr uintptr_t Player_Update = 0xF5F9A8;  // void Player.Update(int i)
constexpr uintptr_t Player_KillMe = 0xFBF6BC;
// static int NPC.NewNPC(IEntitySource source, int X, int Y, int Type, int Start = 0,
//                       float ai0 = 0, float ai1 = 0, float ai2 = 0, float ai3 = 0,
//                       int Target = 255)
constexpr uintptr_t NPC_NewNPC = 0xEE4630;
// void NPC.SetDefaults(int Type, NPCSpawnParams spawnparams). Chamado por
// NewNPC, Transform (0xEE0C44), SetDefaultsFromNetId (0xEDC5F8) e pela rede.
constexpr uintptr_t NPC_SetDefaults = 0xDAF1BC;
// void NPC.UpdateNPC(int i): chamado por Main.DoUpdateInWorld (0xCBBDF0) num
// laço i < 200; é o ÚNICO chamador de NPC.AI no binário (bl em 0xF4A7C8).
constexpr uintptr_t NPC_UpdateNPC = 0xF4A2E8;
constexpr uintptr_t NPC_AI = 0xDCE54C;          // void NPC.AI()
constexpr uintptr_t Main_DoUpdateInWorld = 0xCBBDF0;
// protected void Main.DoUpdate(GameTime): todo frame, no menu e no mundo,
// na thread do jogo (chamado por Main.Update 0xCADB8C).
constexpr uintptr_t Main_DoUpdate = 0xCADDB8;
// static bool Main.get_gameMenu(): lê LocalUserGameState.Instance.gameMenu.
// Instance é [ThreadStatic]: fora da thread do jogo é nulo e o getter lança
// NullReferenceException. Por isso a ponte só lê isso dentro de DoUpdate.
constexpr uintptr_t Main_get_gameMenu = 0xC69BB0;
}  // namespace rva

// Número de parâmetros C# (sem contar `this`), usado em
// il2cpp_class_get_method_from_name para desambiguar overloads.
namespace argc {
constexpr int Player_Hurt = 8;
constexpr int Player_Heal = 1;
constexpr int Main_get_myPlayer = 0;  // Main.myPlayer é propriedade nesta build
constexpr int NPC_NewNPC = 10;
constexpr int NPC_SetDefaults = 2;
constexpr int NPC_AI = 0;
constexpr int Main_DoUpdate = 1;
}  // namespace argc

}  // namespace terraria
