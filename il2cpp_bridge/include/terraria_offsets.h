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
// Main.maxItems (const = 400): itens no mundo. Item.NewItem devolve esse valor
// quando não cria o item.
constexpr int32_t kMaxWorldItems = 400;
// NPCID.Count (const = 688). IDs negativos (-65..-1) também são originais.
constexpr int32_t kNpcVanillaCount = 688;

// --- Campos de instância de Terraria.Player --------------------------------
// Offsets relativos ao ponteiro Player* (incluem o cabeçalho de 0x10 bytes).
// Fonte: dump.cs / player_fields.md.
namespace player_off {
constexpr size_t whoAmI = 0x10;        // int   (herdado de Entity)
constexpr size_t active = 0x14;        // bool  (herdado de Entity)
constexpr size_t position = 0x20;      // Vector2 (herdado de Entity)
constexpr size_t width = 0x48;         // int   (herdado de Entity)
constexpr size_t height = 0x4C;        // int   (herdado de Entity)
constexpr size_t immune = 0x3A8;       // bool
constexpr size_t immuneTime = 0x3AC;   // int
constexpr size_t dead = 0x534;         // bool
constexpr size_t statDefense = 0x6F8;  // int
constexpr size_t statLifeMax = 0x6FC;  // int
constexpr size_t statLifeMax2 = 0x700; // int  (vida máxima com bônus)
constexpr size_t statLife = 0x704;     // int  <-- usado pela ponte
constexpr size_t statMana = 0x708;     // int
constexpr size_t statManaMax2 = 0x710; // int  (mana máxima com bônus)
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
constexpr size_t selectedItem = 0x3F0;  // int   slot do inventário na mão
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
// O que a casca define no SetDefaults e a definição do mod não sobrescreve
// (lidos de um Item de sonda para avaliar a casca: TLMod_ReadItemTraits).
constexpr size_t accessory = 0x6B;       // bool
constexpr size_t potion = 0x6C;          // bool
constexpr size_t consumable = 0x6D;      // bool
constexpr size_t channel = 0x6A;         // bool
constexpr size_t vanity = 0x77;          // bool
constexpr size_t magic = 0x7E;           // bool
constexpr size_t ranged = 0x7F;          // bool
constexpr size_t summon = 0x80;          // bool
constexpr size_t sentry = 0x81;          // bool
constexpr size_t fishingPole = 0x9C;     // int
constexpr size_t bait = 0xA0;            // int
constexpr size_t makeNPC = 0xA4;         // short
constexpr size_t pick = 0xD4;            // int
constexpr size_t axe = 0xD8;             // int
constexpr size_t hammer = 0xDC;          // int
constexpr size_t createTile = 0xE4;      // int   (-1 = não coloca bloco)
constexpr size_t createWall = 0xE8;      // int   (-1 = não coloca parede)
constexpr size_t healLife = 0xF8;        // int
constexpr size_t healMana = 0xFC;        // int
constexpr size_t defense = 0x118;        // int
constexpr size_t headSlot = 0x11C;       // int   (-1 = não é armadura de cabeça)
constexpr size_t bodySlot = 0x120;       // int
constexpr size_t legSlot = 0x124;        // int
constexpr size_t shoot = 0x158;          // int   projétil disparado ao usar
constexpr size_t ammo = 0x160;           // int   AmmoID: este item É munição
constexpr size_t useAmmo = 0x164;        // int   AmmoID que este item CONSOME (arco, arma)
constexpr size_t mana = 0x170;           // int
constexpr size_t buffType = 0x17C;       // int
constexpr size_t mountType = 0x184;      // int   (-1 = não é montaria)
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

// --- Texturas (confirmado no código desta build, não no PC) ------------------------
// Terraria.GameContent.TextureAssets: Asset<Texture2D>[] por tipo. O desenho
// lê TextureAssets.Item[item.type] e TextureAssets.Npc[npc.type] e, em seguida,
// o campo Value do Asset direto (sem chamar get_Value): ver GUIInventory.DrawItem
// (0x95F200: ldr [static+0x560]; ldr [asset+0x28]). Main.LoadItem/LoadNPC só
// pedem o arquivo se o Asset estiver NotLoaded.
// Texture2D aqui é Microsoft.Xna.Framework.Graphics.Texture2D da versão mobile:
// um invólucro de UnityEngine.Texture2D (_unityTexture) com atlas (PackedEntry,
// _textureAtlas). O .ctor(UnityEngine.Texture2D) (0xB38544) copia width/height,
// guarda a textura e marca _textureLoaded, sem atlas.
// UnityEngine.ImageConversion (LoadImage) e Texture2D.LoadRawTextureData/
// SetPixels32 foram removidos pelo stripping desta build: os pixels entram por
// Texture2D.GetWritableImageData (privado) + Apply.
constexpr const char* kTextureAssetsClass = "TextureAssets";     // Terraria.GameContent
constexpr const char* kXnaTexture2DClass = "Texture2D";          // Microsoft.Xna.Framework.Graphics
namespace texture_assets_static_off {
constexpr size_t Item = 0x560;  // Asset<Texture2D>[] (ItemID.Count)
constexpr size_t Npc = 0x570;   // Asset<Texture2D>[] (NPCID.Count)
}  // namespace texture_assets_static_off
namespace xna_texture2d_off {
constexpr size_t Width = 0x44;          // int
constexpr size_t Height = 0x48;         // int
constexpr size_t _unityTexture = 0x78;  // Texture2D (UnityEngine)
}  // namespace xna_texture2d_off
// ReLogic.Content.Asset<Texture2D> é genérico: o dump.cs mostra 0x0 nos campos;
// os offsets são os da instância (il2cpp.h, ReLogic_Content_Asset_Texture2D__Fields).
// A ponte resolve pelo nome na classe da instância. AssetState.Loaded = 2.
namespace asset_texture2d_fields {
constexpr size_t Name = 0x10;   // <Name>k__BackingField
constexpr size_t State = 0x18;  // <State>k__BackingField
constexpr size_t Value = 0x28;  // <Value>k__BackingField
}  // namespace asset_texture2d_fields
constexpr int32_t kAssetStateLoaded = 2;
constexpr int32_t kTextureFormatRGBA32 = 4;  // UnityEngine.TextureFormat
constexpr int32_t kFilterModePoint = 0;      // UnityEngine.FilterMode (arte em pixel)
constexpr int32_t kWrapModeClamp = 1;        // UnityEngine.TextureWrapMode
// PlayerDrawSet é struct: os offsets abaixo são sem o cabeçalho de objeto
// (o il2cpp_field_get_offset devolve +0x10 para campos de tipo-valor).
namespace player_draw_set_off {
constexpr size_t heldItem = 0x218;  // Item: o que DrawPlayer_27_HeldItem desenha na mão
}  // namespace player_draw_set_off

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

// Terraria.ID.ItemID.Sets (classe aninhada): arrays estáticos indexados pelo
// type, criados no .cctor com o tamanho ItemID.Count. Um item de mod herda da
// casca todos eles; estes são os que mudam o comportamento (TLMod_ReadItemTraits).
constexpr const char* kItemIdClass = "ItemID";      // Terraria.ID
constexpr const char* kItemIdSetsClass = "Sets";    // aninhada em ItemID
namespace item_id_sets_static_off {
constexpr size_t ItemsThatShouldNotBeInInventory = 0x28;  // bool[] some do inventário
constexpr size_t ShimmerTransformToItem = 0x50;  // int[]  no Shimmer vira este item (-1 = não)
constexpr size_t IsAKite = 0x90;                 // bool[]
constexpr size_t CanGetPrefixes = 0xA8;          // bool[] aceita prefixo (reforja)
constexpr size_t IsAMaterial = 0xC8;             // bool[] linha "Material" no tooltip
constexpr size_t ToolTipDamageMultiplier = 0xF0; // float[] dano mostrado no tooltip
constexpr size_t IsAPickup = 0xF8;               // bool[] coração/estrela: coletado, nunca no inventário
constexpr size_t IsDrill = 0x100;                // bool[]
constexpr size_t IsChainsaw = 0x108;             // bool[]
constexpr size_t SummonerWeaponThatScalesWithAttackSpeed = 0x118;  // bool[]
constexpr size_t IsFood = 0x120;                 // bool[]
constexpr size_t IsFishingCrate = 0x180;         // bool[] botão direito abre
constexpr size_t Deprecated = 0x1A8;             // bool[]
constexpr size_t CommonCoin = 0x1B8;             // bool[] moeda: junta com o dinheiro
constexpr size_t ExtractinatorMode = 0x1D0;      // int[]  o Extrator aceita (-1 = não)
constexpr size_t StaffMinionSlotsRequired = 0x1D8;  // int[]
constexpr size_t NebulaPickup = 0x1E8;           // bool[]
constexpr size_t gunProj = 0x1F8;                // bool[]
constexpr size_t Yoyo = 0x258;                   // bool[]
constexpr size_t BossBag = 0x2A0;                // bool[] botão direito abre
constexpr size_t OpenableBag = 0x2A8;            // bool[] botão direito abre
}  // namespace item_id_sets_static_off

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
constexpr size_t frame = 0x1C0;     // Rectangle (X, Y, Width, Height): quadro da folha a desenhar
constexpr size_t width = 0x48;      // int   (Entity)
constexpr size_t height = 0x4C;     // int   (Entity)
constexpr size_t dontTakeDamage = 0x208;  // bool  (partes imortais, NPCs invulneráveis)
constexpr size_t townNPC = 0x21C;   // bool
constexpr size_t friendly = 0x238;  // bool
}  // namespace npc_off

// --- Dicionários de nomes internos (IdDictionary, ReLogic.Reflection) --------
namespace item_id_static_off {
constexpr size_t Search = 0x8;   // IdDictionary ItemID.Search ("Zenith" <-> 4956)
}  // namespace item_id_static_off
namespace npc_id_static_off {
constexpr size_t Search = 0x10;  // IdDictionary NPCID.Search ("EyeofCthulhu" <-> 4)
}  // namespace npc_id_static_off

// --- Campos estáticos de Terraria.Main -------------------------------------
// Offset dentro de static_fields da classe; a ponte usa
// il2cpp_field_static_get_value e não este número diretamente.
namespace main_static_off {
constexpr size_t npc = 0x5B0;      // NPC[] Main.npc (200 slots)
constexpr size_t item = 0x5A0;     // Item[] Main.item (itens no mundo: 400 + 1 de folga)
constexpr size_t dayTime = 0x4B8;  // bool   Main.dayTime
constexpr size_t time = 0x4C0;     // double Main.time (ticks desde 4:30 de dia ou 19:30 de noite)
constexpr size_t player = 0x678;   // Player[] Main.player (255 slots)
constexpr size_t netMode = 0x81C;  // int Main.netMode
constexpr size_t chest = 0x5F8;    // Chest[] Main.chest (8000 slots; nulo = sem baú)
constexpr size_t ActiveWorldFileData = 0x730;  // WorldFileData Main.ActiveWorldFileData
constexpr size_t CreativeMenu = 0x50;          // CreativeUI Main.CreativeMenu
constexpr size_t npcFrameCount = 0x1138;       // int[] Main.npcFrameCount (quadros na folha, por tipo)
constexpr size_t itemAnimations = 0x10E8;      // DrawAnimation[] Main.itemAnimations (por type; nulo = sem animação)
constexpr size_t recipe = 0x668;               // Recipe[] Main.recipe
}  // namespace main_static_off

// --- Telas que leem TextureAssets.Item por conta própria (screen_hooks.cpp) -------
// GUIInstance.Active: a instância da interface do mobile, com uma tela por campo.
constexpr const char* kGuiInstanceClass = "GUIInstance";  // namespace global
namespace gui_instance_static_off {
constexpr size_t Active = 0x0;  // GUIInstance
}  // namespace gui_instance_static_off
namespace gui_instance_off {
constexpr size_t GUICraftGuide = 0xB0;  // GUICraftGuide (guia: o item vem de get_guideItem)
constexpr size_t GUIReforge = 0x1A8;    // GUIReforge (o item vem de get_ReforgeItem)
}  // namespace gui_instance_off
namespace gui_crafting_off {
constexpr size_t CraftHover = 0x88;  // Recipe: a receita sob o dedo (ingredientes do tooltip)
}  // namespace gui_crafting_off
namespace gui_craft_guide_popup_off {
constexpr size_t focusGuideRecipe = 0xCC;      // int   (-1 = mostra o item da guia)
constexpr size_t availableGuideRecipe = 0xD0;  // int[] índices em Main.recipe
}  // namespace gui_craft_guide_popup_off
namespace recipe_off {
constexpr size_t createItem = 0x10;    // Item
constexpr size_t requiredItem = 0x18;  // Item[]
}  // namespace recipe_off
namespace item_snippet_off {
constexpr size_t _item = 0x30;  // Item  (ItemTagHandler.ItemSnippet: [i:ID] no chat)
}  // namespace item_snippet_off

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

// --- Desenho: onde esta build lê TextureAssets.Item / TextureAssets.Npc ----------
// Inventário, baús e bancos (GUIInventory.ItemDraw -> DrawItem).
constexpr uintptr_t GUIInventory_DrawItem = 0x95E6E4;
constexpr uintptr_t GUIHotbar_DrawHotbarItem = 0x83BD64;  // barra rápida
constexpr uintptr_t Main_DrawItem = 0xCFA928;             // item no chão (Main.DrawItems)
// Ícone genérico (ItemSlot.Draw, UIItemIcon, partículas de transferência, tiles).
constexpr uintptr_t ItemSlot_DrawItemIcon = 0x1632310;
// Item na mão / em uso (LegacyPlayerRenderer.DrawPlayer_UseNormalLayers).
constexpr uintptr_t PlayerDrawLayers_DrawPlayer_27_HeldItem = 0x15C0EDC;
// void Main.DrawNPCDirect(SpriteBatch, NPC, bool behindTiles, Vector2 screenPos,
// LightMap, Rectangle*): o caminho do mundo, Main.DrawNPC (0xCD4410) termina
// aqui com `b`. O overload sem LightMap (0xBCB734) é outra cópia inteira, chamada
// só pelo bestiário (UnlockableNPCEntryIcon.Draw) e pelos créditos
// (NPCSegment.Draw), com NPCs próprios fora de Main.npc: ficam com a casca.
constexpr uintptr_t Main_DrawNPCDirect = 0xBFC154;
// Telas que leem TextureAssets.Item[item.type] direto (não passam pelos hooks acima).
// Botão de transação com o item (loja: item à venda; pesquisa: item selecionado...).
constexpr uintptr_t GUITransactionButton_Draw = 0x12220F8;
constexpr uintptr_t GUIReforgePopup_DrawItem = 0x9D2984;        // item da reforja
constexpr uintptr_t GUIResearch_DrawResearchItem = 0x9D7164;    // item na pesquisa da Jornada
constexpr uintptr_t GUIResearch_CanBeResearched = 0x9D4C10;     // só olha o type: item de mod bloqueado
// static void MouseText_DrawItemTooltip_GetLinesInfo(Item, ref int yoyoLogo, ref int researchLine,
//     ref int materialsLine, float oldKB, ref int numLines, string[] toolTipLine, bool[] preFixLine,
//     bool[] badPreFixLine, ref int setBonusLine): monta as linhas do tooltip. Na Jornada
// pergunta ao catálogo abaixo pelo type (0xCC972C) para a linha "Pesquise mais N".
constexpr uintptr_t Main_MouseText_DrawItemTooltip_GetLinesInfo = 0xCC90D8;
// bool TryGetSacrificeCountCapToUnlockInfiniteItems(int itemId, out int amountNeeded)
constexpr uintptr_t CreativeItemSacrificesCatalog_TryGetSacrificeCountCapToUnlockInfiniteItems = 0x8F4290;
constexpr uintptr_t GUICraftGuidePopup_DrawItem = 0x15A5384;    // item da guia / resultado da receita
constexpr uintptr_t GUICrafting_MaterialItemDraw = 0x73AD78;    // ingrediente da receita em foco
constexpr uintptr_t GUICrafting_MaterialItemTooltipDraw = 0x73A3AC;  // ingrediente no tooltip da receita
// Controles de toque: botão de uso com o item selecionado e o botão do gancho.
constexpr uintptr_t GUIVirtualInputController_DrawControls = 0x9EF6D0;
constexpr uintptr_t GUIQuickActionButton_Draw = 0x10604EC;      // botões rápidos (gancho: Grapple, GrappleFire)
constexpr uintptr_t Main_DrawInterface_40_InteractItemIcon = 0xD0F8B8;  // ícone do item (e munição) junto do cursor
constexpr uintptr_t ItemSnippet_UniqueDraw = 0x97F0D8;           // [i:ID] no chat (mede pela textura)
// Nome de NPC: o jogo só lê Lang pelo netID (o da casca).
constexpr uintptr_t NPC_get_TypeName = 0xED9240;   // Lang.GetNPCNameValue(netID): hover, barra, mapa, morte
constexpr uintptr_t NPC_GetTypeNetName = 0xEDB0BC; // NetworkText.FromKey(...): "X despertou!", "X foi derrotado"
// Ferramentas de teste (mod tlmod.devtools). Chamadas por il2cpp_runtime_invoke, sem hook.
constexpr uintptr_t Lang_GetItemNameValue = 0x11F56B0;  // nome do item no idioma do jogo
constexpr uintptr_t Lang_GetNPCNameValue = 0x11F5298;   // nome do NPC no idioma do jogo
constexpr uintptr_t IdDictionary_GetName = 0x98D538;    // nome interno (ItemID/NPCID.Search)
constexpr uintptr_t Main_SkipToTime = 0xD72700;         // static void SkipToTime(int, bool): poder de hora da Jornada
// double NPC.StrikeNPCNoInteraction(int Damage, float knockBack, int hitDirection,
//                                   bool crit, bool noEffect, bool fromNet)
constexpr uintptr_t NPC_StrikeNPCNoInteraction = 0xEE2BF0;
// static int Item.NewItem(IEntitySource source, int X, int Y, int Width, int Height, int Type,
//     int Stack, bool noBroadcast, int pfix, bool noGrabDelay, bool reverseLookup): o item no
// mundo, dentro do retângulo; devolve o índice em Main.item (400 = não criou). Ignora o
// source (o registrador x0 é sobrescrito no prólogo): vai nulo, como no NPC.NewNPC.
constexpr uintptr_t Item_NewItem = 0xBA8CDC;
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
constexpr int Lang_GetItemNameValue = 1;
constexpr int Lang_GetNPCNameValue = 1;
constexpr int IdDictionary_GetName = 1;
constexpr int Main_SkipToTime = 2;
constexpr int NPC_StrikeNPCNoInteraction = 6;
constexpr int Item_NewItem = 11;  // o único overload de 11 (os outros têm 8, 9 e 10)
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
constexpr int GUIInventory_DrawItem = 7;
constexpr int GUIHotbar_DrawHotbarItem = 7;
constexpr int Main_DrawItem = 2;
constexpr int ItemSlot_DrawItemIcon = 7;
constexpr int PlayerDrawLayers_DrawPlayer_27_HeldItem = 1;
constexpr int Main_DrawNPCDirect = 6;
constexpr int GUITransactionButton_Draw = 9;    // o overload com Item (os outros têm 10, 11 e 13)
constexpr int GUIReforgePopup_DrawItem = 0;
constexpr int GUIResearch_DrawResearchItem = 0;
constexpr int GUIResearch_CanBeResearched = 2;
constexpr int Main_MouseText_DrawItemTooltip_GetLinesInfo = 10;
constexpr int CreativeItemSacrificesCatalog_TryGetSacrificeCountCapToUnlockInfiniteItems = 2;
constexpr int GUICraftGuidePopup_DrawItem = 0;
constexpr int GUICrafting_MaterialItemDraw = 4;
constexpr int GUICrafting_MaterialItemTooltipDraw = 4;
constexpr int GUIVirtualInputController_DrawControls = 0;
constexpr int GUIQuickActionButton_Draw = 4;    // 2 overloads de 4: o do QuickActionControl
constexpr int Main_DrawInterface_40_InteractItemIcon = 0;
constexpr int ItemSnippet_UniqueDraw = 6;
constexpr int NPC_get_TypeName = 0;
constexpr int NPC_GetTypeNetName = 0;
}  // namespace argc

}  // namespace terraria
