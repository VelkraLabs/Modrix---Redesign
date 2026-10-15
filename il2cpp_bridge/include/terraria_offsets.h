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
// Itens gravados no .plr (Player.InternalSavePlayerFile). Laços do save:
// armor/dye/miscEquips/miscDyes inteiros, inventory 0..57, bancos 0..39.
constexpr size_t armor = 0x3F8;        // Item[] (20: armadura, acessórios, vaidade)
constexpr size_t dye = 0x400;          // Item[] (10)
constexpr size_t miscEquips = 0x408;   // Item[] (5: pet, luz, carrinho, montaria, gancho)
constexpr size_t miscDyes = 0x410;     // Item[] (5)
constexpr size_t inventory = 0x4A0;    // Item[] (59; o save grava 0..57)
constexpr size_t bank = 0x4B8;         // Chest (Porquinho)
constexpr size_t bank2 = 0x4C0;        // Chest (Cofre)
constexpr size_t bank3 = 0x4C8;        // Chest (Forja do Defensor)
constexpr size_t bank4 = 0x4D0;        // Chest (Cofre do Vazio)
constexpr size_t loadStatus = 0x1C0;   // int  (0 = LoadPlayer leu o arquivo sem erro)
constexpr size_t Loadouts = 0xD60;     // EquipmentLoadout[] (3)
// Item[] (4): o que LoadPlayer leu da seção "temporária" (mouse, sacrifício
// da Jornada, Guia, Reforja). No save o jogo grava Main.mouseItem etc.
constexpr size_t _temporaryItemSlots = 0xD78;
}  // namespace player_off

// --- Terraria.Item / Chest / EquipmentLoadout --------------------------------
// O save grava netID (int32 no .plr, int32 nos baús do .wld), stack e prefix.
namespace item_off {
constexpr size_t width = 0x48;           // int   (herdado de Entity)
constexpr size_t height = 0x4C;          // int   (herdado de Entity)
constexpr size_t _nameOverride = 0x58;   // string: se não nulo, get_Name devolve este
constexpr size_t favorited = 0x69;       // bool
constexpr size_t melee = 0x7D;           // bool
constexpr size_t type = 0xBC;            // int   índice de todas as tabelas por item
constexpr size_t useStyle = 0xC1;        // byte  (1 = golpe)
constexpr size_t useAnimation = 0xC4;    // int
constexpr size_t useTime = 0xC8;         // int
constexpr size_t stack = 0xCC;           // int
constexpr size_t maxStack = 0xD0;        // int
constexpr size_t damage = 0xF0;          // int
constexpr size_t ToolTip = 0x138;        // ItemTooltip
constexpr size_t rare = 0x14C;           // int
constexpr size_t value = 0x178;          // int
constexpr size_t netID = 0x198;          // int  <-- o que vai para o arquivo
constexpr size_t prefix = 0x1A0;         // byte
}  // namespace item_off
namespace chest_off {
constexpr size_t item = 0x10;  // Item[] (40)
constexpr size_t x = 0x18;     // int
constexpr size_t y = 0x1C;     // int
}  // namespace chest_off
namespace loadout_off {
constexpr size_t Armor = 0x10;  // Item[] (20)
constexpr size_t Dye = 0x18;    // Item[] (10)
}  // namespace loadout_off

// --- Item de mod: casca ----------------------------------------------------------
// O item de mod roda numa casca: type = um item original (texturas, ItemID.Sets,
// Lang, ContentSamples, tabelas do mobile só veem IDs < 5456) e netID = o ID de
// mod. O jogo já separa os dois nas variantes antigas (netID < 0). Quem lê netID:
// netDefaults/Refresh (-> SetDefaults, com hook), IsTheSameAs/IsNotTheSameAs
// (empilhar, depositar, pegar: um item de mod não se mistura com a casca),
// Recipe, Serialize/save (o runtime tira do arquivo) e RebuildTooltip, o único
// que indexa uma tabela com ele (Lang.GetTooltip, bounds-checked): hook.
// Casca padrão: Lente (ItemID.Lens = 38), material sem uso, sem tile, sem tiro.
constexpr int32_t kDefaultItemShell = 38;
constexpr const char* kItemTooltipClass = "ItemTooltip";      // Terraria.UI
constexpr const char* kLocalizedTextClass = "LocalizedText";  // Terraria.Localization
namespace item_tooltip_off {
constexpr size_t _validatorKey = 0x18;  // ulong: != ItemTooltip._globalValidatorKey -> refaz as linhas
constexpr size_t _text = 0x20;          // LocalizedText: as linhas saem de _text.Value
}  // namespace item_tooltip_off
namespace item_tooltip_static_off {
constexpr size_t None = 0x0;                 // ItemTooltip (sem linhas)
constexpr size_t _globalValidatorKey = 0x10; // ulong
}  // namespace item_tooltip_static_off

// --- Terraria.IO.FileData / PlayerFileData -----------------------------------
// Caminho do arquivo que o jogo grava/lê (o .plr de um personagem, o .wld de
// um mundo). Mundo ativo: Main.ActiveWorldFileData.
constexpr const char* kIoNamespace = "Terraria.IO";
constexpr const char* kFileDataClass = "FileData";
constexpr const char* kPlayerFileDataClass = "PlayerFileData";
constexpr const char* kWorldFileClass = "WorldFile";
constexpr const char* kItemClass = "Item";
constexpr const char* kChestClass = "Chest";
constexpr const char* kEquipmentLoadoutClass = "EquipmentLoadout";
constexpr const char* kWorldGenClass = "WorldGen";
namespace file_data_off {
constexpr size_t _path = 0x10;         // string
constexpr size_t _isCloudSave = 0x18;  // bool
}  // namespace file_data_off
namespace player_file_data_off {
constexpr size_t _player = 0x40;  // Player
}  // namespace player_file_data_off
// static bool WorldGen.loadFailed (static_fields+0xC5): LoadWorld não conseguiu ler.
namespace world_gen_static_off {
constexpr size_t loadFailed = 0xC5;  // bool
}  // namespace world_gen_static_off
// Main.maxChests (const = 8000): tamanho de Main.chest[].
constexpr int32_t kMaxChests = 8000;
// ItemID.Count (const = 5456). Item.SetDefaults troca type >= 5456 por 0 (ar:
// netID = stack = 0), e todo load passa por ele (netDefaults): um ID de mod no
// arquivo vira ar e o item some, sem erro. Inventário e bancos conferem antes
// (id >= 5456 -> netDefaults(0)). Em SaveChests,
// ItemID.Sets.ItemsThatShouldNotBeInInventory[type] estoura com type >= 5456.
constexpr int32_t kItemVanillaCount = 5456;

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
constexpr size_t chest = 0x5F8;    // Chest[] Main.chest (8000 slots; nulo = sem baú)
constexpr size_t ActiveWorldFileData = 0x730;  // WorldFileData Main.ActiveWorldFileData
constexpr size_t CreativeMenu = 0x50;          // CreativeUI Main.CreativeMenu
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

// --- Save/load (formato idêntico ao do PC 1.4.4) ---------------------------
// static void Player.SavePlayer(PlayerFileData, bool skipMapSave, bool forceSave):
// chamado por WorldGen.saveToonWhilePlayingCallBack (0x130ED54),
// SaveAndQuitCallBack (0x130C8CC), Netplay.InnerClientLoop, PlayerFileData.CreateAndSave,
// PlayerFileData.Rename e GUIPlayerSelectMenu.CloseNameEditAndSave. Os callbacks
// do WorldGen rodam no ThreadPool, com o jogo rodando em paralelo.
constexpr uintptr_t Player_SavePlayer = 0x1043A9C;
// private static void Player.InternalSavePlayerFile(PlayerFileData): único chamador
// é SavePlayer. Copia o .plr para .bak, cifra com Rijndael e grava, nesta ordem:
// armor/dye (netID int32, prefix byte), inventory 0..57 (netID int32, stack int32,
// prefix byte, favorited bool), miscEquips/miscDyes (netID, prefix), bank/bank2/bank3
// 0..39 (netID, stack, prefix), bank4 0..39 (+favorited), depois
// SaveTemporaryItemSlotContents (0x104624C) e EquipmentLoadout.Serialize (0x12AC9CC),
// que usam Item.Serialize (0xBB8988: netID int32, stack int32, prefix byte).
constexpr uintptr_t Player_InternalSavePlayerFile = 0x1044040;
// static PlayerFileData Player.LoadPlayer(string playerPath, bool cloudSave):
// chamado por Player.GetFileData (0x104E554), que Main.LoadPlayers usa para listar
// os personagens no menu.
constexpr uintptr_t Player_LoadPlayer = 0x1049FA8;
// static void WorldFile.SaveWorld(bool useCloudSaving, bool resetTime, WorldSaveContext)
// -> InternalSaveWorld (único chamador) -> SaveWorld_Version2 (0x14F2BA8) -> SaveChests.
constexpr uintptr_t WorldFile_SaveWorld = 0x14F15E8;
constexpr uintptr_t WorldFile_InternalSaveWorld = 0x14F1AC8;
// private static int WorldFile.SaveChests(BinaryWriter): (short)count, (short)40;
// por baú x int32, y int32, nome; por item (short)stack e, se stack > 0,
// netID int32 + prefix byte.
constexpr uintptr_t WorldFile_SaveChests = 0x14F83CC;
// static void WorldFile.LoadWorld(bool loadFromCloud): chamado por
// WorldGen.playWorldCallBack (0x130D624), serverLoadWorldCallBack e
// RenameWorldCallBack. Lê os baús em LoadChests (0x14FD70C).
constexpr uintptr_t WorldFile_LoadWorld = 0x14EC048;
constexpr uintptr_t WorldFile_LoadChests = 0x14FD70C;
// Itens "temporários" que o save do personagem grava (Main.* lê
// LocalUserGameState.Instance, que é [ThreadStatic]).
constexpr uintptr_t Main_get_mouseItem = 0xC68290;
constexpr uintptr_t Main_get_guideItem = 0xC685F4;
constexpr uintptr_t Main_get_reforgeItem = 0xC686A0;
constexpr uintptr_t CreativeUI_GetItemByIndex = 0x8F643C;

// --- Itens ---------------------------------------------------------------------
// void Item.SetDefaults(int Type, bool noMatCheck, ItemVariant variant): todos os
// caminhos passam por aqui: SetDefaults(int) (0xBA8CD0) é um `b` para cá com
// false/null, e netDefaults (0xBAB378) também. Depois de ResetStats o próprio
// SetDefaults faz `if (type >= 5456) type = 0` e indexa ~dezenas de tabelas por type.
constexpr uintptr_t Item_SetDefaults = 0xBAD034;
// void Item.RebuildTooltip(): ToolTip = Lang.GetTooltip(netID). Chamado pelo fim
// de SetDefaults e pelas telas GUICrafting.CraftButtonOver / GUIItemDuplication.
constexpr uintptr_t Item_RebuildTooltip = 0xBAAFE8;
// static ItemTooltip Lang.GetTooltip(int): _itemTooltipCache[id], com bounds check.
constexpr uintptr_t Lang_GetTooltip = 0x11F5414;
// string Item.get_Name(): _nameOverride ?? Lang.GetItemNameValue(type).
constexpr uintptr_t Item_get_Name = 0xBA8724;
constexpr uintptr_t Item_Prefix = 0xBA9A80;           // bool Item.Prefix(int prefixWeWant)
constexpr uintptr_t Item_IsTheSameAs = 0xBB87D0;      // netID == && type ==
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
constexpr int Player_InternalSavePlayerFile = 1;
constexpr int Player_LoadPlayer = 2;
constexpr int WorldFile_SaveWorld = 3;
constexpr int WorldFile_InternalSaveWorld = 2;
constexpr int WorldFile_LoadWorld = 1;
constexpr int Main_get_mouseItem = 0;
constexpr int Main_get_guideItem = 0;
constexpr int Main_get_reforgeItem = 0;
constexpr int CreativeUI_GetItemByIndex = 1;
// Item.SetDefaults tem 2 overloads: o hook é no de 3 parâmetros; o de 1 é
// chamado via runtime_invoke (TLMod_SetItemDefaults) e cai no hook.
constexpr int Item_SetDefaults = 3;
constexpr int Item_RebuildTooltip = 0;
constexpr int Lang_GetTooltip = 1;
constexpr int Item_get_Name = 0;
constexpr int Item_Prefix = 1;
constexpr int Item_IsTheSameAs = 1;
}  // namespace argc

}  // namespace terraria
