// API pública da ponte TLMod <-> IL2CPP (Terraria Mobile).
#pragma once

#include <cstddef>
#include <cstdint>

#include "il2cpp_api.h"

// Tudo abaixo é exportado da .so (o resto do código usa visibility=hidden).
#pragma GCC visibility push(default)
extern "C" {

// Assinatura nativa que o IL2CPP gera para
//   double Terraria.Player.Hurt(PlayerDeathReason, int, int, bool, bool, bool, int, bool)
// Em métodos de instância o `this` vem primeiro e o IL2CPP (2021.3) acrescenta
// um `const MethodInfo*` oculto no final. Assinatura do script.json do dump:
//   double Terraria_Player__Hurt(Terraria_Player_o* __this,
//       Terraria_DataStructures_PlayerDeathReason_o* damageSource, int32_t Damage,
//       int32_t hitDirection, bool pvp, bool quiet, bool Crit, int32_t cooldownCounter,
//       bool dodgeable, const MethodInfo* method);
typedef double (*PlayerHurtFn)(Il2CppObject* self, Il2CppObject* damageSource, int32_t damage,
                               int32_t hitDirection, bool pvp, bool quiet, bool crit,
                               int32_t cooldownCounter, bool dodgeable, const MethodInfo* method);

// Contexto entregue ao callback do mod a cada Player.Hurt.
struct TLModHurtEvent {
    Il2CppObject* player;
    int32_t damage;         // dano pedido (antes de defesa)
    int32_t hitDirection;
    bool pvp;
    bool crit;
    int32_t lifeBefore;     // statLife antes do Hurt original
    int32_t lifeAfter;      // statLife depois (preenchido só no pós-callback)
    double result;          // retorno do Hurt original (dano efetivo)
};

// Callback do mod. Chamado duas vezes por Hurt:
//   pre  (after == false): pode alterar ev->damage; retornar false cancela o golpe.
//   post (after == true):  só observa; o retorno é ignorado.
typedef bool (*TLModHurtCallback)(TLModHurtEvent* ev, bool after);

// Função para instalar um inline hook. No Android a ponte usa o ShadowHook
// (src/hook_shadowhook.cpp); no teste de host, um instalador falso.
// Deve gravar em *original o endereço que chama a função original. O slot
// `original` aponta para memória global da ponte, válida pelo processo todo:
// a engine pode guardá-lo e reescrevê-lo depois (ShadowHook, modo multi).
typedef bool (*TLModHookInstaller)(void* target, void* replacement, void** original);

// Configuração (chamar antes de TLMod_Init / TLMod_StartInitThread).
void TLMod_SetLibraryName(const char* name);  // padrão: "libil2cpp.so"
void TLMod_SetHookInstaller(TLModHookInstaller installer);
void TLMod_SetHurtCallback(TLModHurtCallback cb);

// Inicialização síncrona: espera a libil2cpp.so e o runtime subirem (até
// timeoutMs), resolve Terraria.Player e instala o hook em Player.Hurt.
bool TLMod_Init(int timeoutMs);

// Dispara TLMod_Init numa thread separada (usado pelo construtor da .so).
void TLMod_StartInitThread(void);

// Leitura de statLife pelo offset resolvido em runtime (fallback: 0x704).
int32_t TLMod_GetStatLife(Il2CppObject* player);

// Player local via Main.player[Main.myPlayer] (usa il2cpp_runtime_invoke).
// Precisa rodar numa thread anexada ao IL2CPP.
Il2CppObject* TLMod_GetLocalPlayer(void);

// Main.player[index]; nulo se fora da faixa ou se a ponte não está pronta.
Il2CppObject* TLMod_GetPlayerByIndex(int32_t index);

// Entity.whoAmI do player (índice em Main.player); -1 para nulo.
int32_t TLMod_GetPlayerIndex(Il2CppObject* player);

// Espera, sem iniciar nada, até outra thread concluir TLMod_Init.
bool TLMod_WaitReady(int timeoutMs);

// Anexa a thread atual ao IL2CPP (necessário antes de runtime_invoke fora
// das threads do jogo). Não faz nada se já estiver anexada.
bool TLMod_AttachCurrentThread(void);

// Chama Player.Heal(amount) via il2cpp_runtime_invoke.
bool TLMod_Heal(Il2CppObject* player, int32_t amount);

// Função que o hook de Player.Hurt aponta. Exportada para que um instalador
// externo (ex.: script Frida, ou o loader do TLMod) possa usá-la diretamente.
double TLMod_Hooked_PlayerHurt(Il2CppObject* self, Il2CppObject* damageSource, int32_t damage,
                                int32_t hitDirection, bool pvp, bool quiet, bool crit,
                                int32_t cooldownCounter, bool dodgeable, const MethodInfo* method);

// Endereço da função original (trampoline) após o hook; nulo se não instalado.
PlayerHurtFn TLMod_OriginalPlayerHurt(void);

// ===========================================================================
// NPCs: pontos onde um NPC/boss de mod se pluga
// ===========================================================================
//
// Fluxo de um NPC de mod (ID fora da faixa original, ex.: 4096):
//
//   NPC.NewNPC(Type=4096)                 hook pré: remap_spawn_type(4096) -> 4
//     └ NewNPC original com Type=4 ("casca" original: textura, hitbox, som)
//         └ NPC.SetDefaults(4)            hook pré: on_set_defaults (slot muda de dono)
//   NewNPC retorna i                      hook pós: on_spawned(npc, i, 4096, 4)
//                                         -> quem chamou registra "slot i = NPC de mod 4096"
//   Main.DoUpdateInWorld -> NPC.UpdateNPC(i) -> NPC.AI()
//                                         hook pré: on_ai(npc) a cada tick
//                                         -> true = comportamento do mod substitui a AI original
//
// O tipo de mod nunca chega ao SetDefaults original: o jogo indexa arrays por
// tipo (texturas, npcFrameCount, sons...) e um ID >= NPCID.Count (688)
// leria fora deles.

// Struct C# Terraria.NPCSpawnParams, passada POR VALOR para SetDefaults.
// Layout do il2cpp.h/dump.cs: 4 campos de 8 bytes. Com 32 bytes (> 16), a ABI
// AArch64 passa um ponteiro para uma cópia; declarar o layout exato faz o
// compilador gerar a mesma convenção no hook.
typedef struct TLModNullableF32 { bool hasValue; float value; } TLModNullableF32;
typedef struct TLModNullableI32 { bool hasValue; int32_t value; } TLModNullableI32;
typedef struct NPCSpawnParams {
    TLModNullableF32 sizeScaleOverride;                    // 0x00
    TLModNullableI32 playerCountForMultiplayerDifficultyOverride;  // 0x08
    Il2CppObject* gameModeData;                             // 0x10 (GameModeData, classe)
    TLModNullableF32 strengthMultiplierOverride;           // 0x18
} NPCSpawnParams;

// Assinaturas nativas (script.json do dump). `this` primeiro em métodos de
// instância; MethodInfo* oculto no fim.
typedef int32_t (*NpcNewNpcFn)(Il2CppObject* source, int32_t x, int32_t y, int32_t type,
                               int32_t start, float ai0, float ai1, float ai2, float ai3,
                               int32_t target, const MethodInfo* method);
typedef void (*NpcSetDefaultsFn)(Il2CppObject* self, int32_t type, NPCSpawnParams spawnparams,
                                 const MethodInfo* method);
typedef void (*NpcAiFn)(Il2CppObject* self, const MethodInfo* method);

typedef struct TLModNpcCallbacks {
    // Pré-NewNPC: devolve o tipo que o jogo deve realmente criar. Para tipos
    // originais, devolva o próprio type. Nulo = sem troca.
    int32_t (*remap_spawn_type)(int32_t requested_type);
    // Pós-NewNPC. index == 200 (Main.maxNPCs) quando o jogo não achou slot;
    // nesse caso npc é nulo.
    void (*on_spawned)(Il2CppObject* npc, int32_t index, int32_t requested_type,
                       int32_t spawned_type);
    // Pré-SetDefaults: o NPC vai ser (re)inicializado como `type`. index é a
    // posição em Main.npc[], ou -1 para NPCs fora dele (ex.: amostras do bestiário).
    void (*on_set_defaults)(Il2CppObject* npc, int32_t index, int32_t type);
    // Pré-AI, uma vez por NPC ativo por tick (thread do jogo).
    // Retornar true pula a AI original nesse tick.
    bool (*on_ai)(Il2CppObject* npc, int32_t index);
} TLModNpcCallbacks;

// Copia os callbacks (a struct pode ser temporária). Nulo desliga todos.
void TLMod_SetNpcCallbacks(const TLModNpcCallbacks* callbacks);

// Visão plana dos campos de NPC que a camada de mods usa.
typedef struct TLModNpcFields {
    int32_t index;     // whoAmI
    bool active;
    int32_t type;
    float x, y;        // position (canto superior esquerdo, pixels)
    float vx, vy;      // velocity
    int32_t life, lifeMax, damage, defense;
    int32_t target;    // índice do player
    float ai[4];
} TLModNpcFields;

bool TLMod_ReadNpc(Il2CppObject* npc, TLModNpcFields* out);
// Escreve posição, velocidade, life, lifeMax, damage, defense, target e ai[].
// Não muda index, active nem type.
bool TLMod_WriteNpc(Il2CppObject* npc, const TLModNpcFields* in);

Il2CppObject* TLMod_GetNpcByIndex(int32_t index);    // Main.npc[index]
int32_t TLMod_FindNpcIndex(Il2CppObject* npc);       // posição em Main.npc[] ou -1

// Chama NPC.NewNPC via il2cpp_runtime_invoke (passa pelos hooks acima).
// Só na thread do jogo. Devolve o índice ou 200 se não houver slot.
// `source` (IEntitySource) vai nulo.
int32_t TLMod_SpawnNpc(int32_t x, int32_t y, int32_t type);

// ===========================================================================
// Estado do mundo / frame
// ===========================================================================

// Chamado no início de Main.DoUpdate (RVA 0xCADDB8): uma vez por frame, na
// thread do jogo, tanto no menu quanto no mundo. É o lugar para tirar
// "fotografias" de estado que outras threads (UI) vão consultar. Nulo desliga.
typedef void (*TLModFrameCallback)(void);
void TLMod_SetFrameCallback(TLModFrameCallback cb);

// Só na thread do jogo (ex.: dentro do TLModFrameCallback): 1 = menu,
// 0 = dentro de um mundo, -1 = indisponível. Lê LocalUserGameState.Instance
// (ThreadStatic) .gameMenu; em outra thread Instance é nulo e o resultado é -1.
int32_t TLMod_IsGameMenu(void);

// Campo estático comum (não ThreadStatic), resolvido uma vez e lido muitas.
// namespaze pode ser "" (namespace global). Nulo se não existir.
typedef struct TLModStaticField TLModStaticField;
TLModStaticField* TLMod_FindStaticField(const char* namespaze, const char* klass, const char* field);
bool TLMod_ReadStaticBool(const TLModStaticField* field, bool* out);

// ===========================================================================
// Save/load: personagem (.plr) e mundo (.wld)
// ===========================================================================
//
// O jogo grava o ID numérico do item (netID) no arquivo. Para que um ID de mod
// nunca chegue lá, quem assina os callbacks abaixo tira os itens de mod antes
// do save e os devolve depois. Hooks (RVAs em terraria_offsets.h):
//
//   Player.InternalSavePlayerFile(PlayerFileData)   antes / depois
//   Player.LoadPlayer(string path, bool cloud)      depois (devolve PlayerFileData)
//   WorldFile.InternalSaveWorld(bool cloud, bool)   antes / depois
//   WorldFile.LoadWorld(bool cloud)                 depois
//
// Os saves rodam no ThreadPool (saveToonWhilePlayingCallBack, saveAndPlayCallBack,
// SaveAndQuitCallBack) com a thread do jogo rodando em paralelo: o antes e o
// depois de um save chegam sempre na MESMA thread, mas o jogo pode mexer nos
// itens entre os dois.

typedef enum TLModSaveKind { TLMOD_SAVE_PLAYER = 0, TLMOD_SAVE_WORLD = 1 } TLModSaveKind;
typedef enum TLModSavePhase {
    TLMOD_BEFORE_SAVE = 0,
    TLMOD_AFTER_SAVE = 1,  // também quando o save lançou exceção (ok = false)
    TLMOD_AFTER_LOAD = 2,
} TLModSavePhase;

typedef struct TLModSaveEvent {
    TLModSaveKind kind;
    const char* path;      // UTF-8, válido só durante o callback; nunca nulo
    bool cloud;
    // Personagem: o Player gravado/carregado (do PlayerFileData). Mundo: nulo.
    Il2CppObject* player;
    // Depois do save: false se o jogo lançou exceção. Depois do load:
    // false se o jogo não conseguiu ler (Player.loadStatus != 0, WorldGen.loadFailed).
    bool ok;
} TLModSaveEvent;

// Chamado na thread que salva/carrega. Não pode lançar exceção.
typedef void (*TLModSaveCallback)(const TLModSaveEvent* ev, TLModSavePhase phase);
void TLMod_SetSaveCallback(TLModSaveCallback cb);

// Onde ficam os itens que o jogo grava. `group`: índice do loadout
// (LOADOUT_*), índice em Main.chest (CHEST); 0 nos outros.
typedef enum TLModItemContainer {
    TLMOD_ITEMS_INVENTORY = 0,  // Player.inventory (o save grava 0..57)
    TLMOD_ITEMS_ARMOR,          // Player.armor
    TLMOD_ITEMS_DYE,            // Player.dye
    TLMOD_ITEMS_MISC_EQUIPS,    // Player.miscEquips
    TLMOD_ITEMS_MISC_DYES,      // Player.miscDyes
    TLMOD_ITEMS_BANK,           // Player.bank.item  (Porquinho)
    TLMOD_ITEMS_BANK2,          // Player.bank2.item (Cofre)
    TLMOD_ITEMS_BANK3,          // Player.bank3.item (Forja do Defensor)
    TLMOD_ITEMS_BANK4,          // Player.bank4.item (Cofre do Vazio)
    TLMOD_ITEMS_LOADOUT_ARMOR,  // Player.Loadouts[group].Armor
    TLMOD_ITEMS_LOADOUT_DYE,    // Player.Loadouts[group].Dye
    // O que o save grava como "temporário": 0 Main.mouseItem,
    // 1 Main.CreativeMenu.GetItemByIndex(0), 2 Main.guideItem, 3 Main.reforgeItem.
    // Globais da thread do jogo (LocalUserGameState): ler na thread do save.
    TLMOD_ITEMS_HELD,
    TLMOD_ITEMS_TEMPORARY,      // Player._temporaryItemSlots: o que o load leu de HELD
    TLMOD_ITEMS_CHEST,          // Main.chest[group].item
    TLMOD_ITEMS_CONTAINER_COUNT
} TLModItemContainer;

// Número de slots; -1 se o contêiner não existe (player nulo, baú vazio...).
int32_t TLMod_ItemSlotCount(Il2CppObject* player, TLModItemContainer container, int32_t group);
// O Item no slot; nulo se não existe. Em HELD chama os getters do jogo.
Il2CppObject* TLMod_ItemSlot(Il2CppObject* player, TLModItemContainer container, int32_t group,
                             int32_t index);

// Campos de Terraria.Item que o save grava.
typedef struct TLModItemFields {
    int32_t type;
    int32_t netID;  // o que vai para o arquivo; igual a type exceto em variantes antigas (< 0)
    int32_t stack;
    uint8_t prefix;
    bool favorited;
} TLModItemFields;
bool TLMod_ReadItem(Il2CppObject* item, TLModItemFields* out);
// Escreve SÓ esses campos (não chama SetDefaults): serve para esvaziar um
// slot durante o save e devolver depois exatamente o que estava lá.
bool TLMod_WriteItem(Il2CppObject* item, const TLModItemFields* in);

// Main.chest: tamanho (8000) e posição do baú (false se Main.chest[index] é nulo).
int32_t TLMod_ChestCount(void);
bool TLMod_ChestPosition(int32_t index, int32_t* x, int32_t* y);

// ===========================================================================
// Itens de mod: casca original + identidade em netID
// ===========================================================================
//
//   Item.SetDefaults(8192)            hook pré: remap_type(8192) -> 38 (casca)
//     └ SetDefaults original com 38   o jogo só indexa tabelas com 38
//   hook pós: netID = 8192; on_defaults(item, 8192, 38)
//                                     -> quem chamou aplica dano, pilha, nome, tooltip...
//   Item.RebuildTooltip               netID fora da faixa original: on_rebuild_tooltip
//                                     no lugar do original (Lang.GetTooltip(netID) estouraria)
//
// type nunca sai da faixa original. netID carrega a identidade: Clone
// (MemberwiseClone) o copia, IsTheSameAs o compara (empilhar/depositar não
// mistura com a casca) e netDefaults/Refresh(netID) passam de novo pelo hook.

typedef struct TLModItemCallbacks {
    // Pré-SetDefaults: tipo pedido -> tipo que o jogo deve montar. Para itens
    // originais, devolva o próprio tipo. Pode rodar em qualquer thread.
    int32_t (*remap_type)(int32_t requested_type);
    // Pós-SetDefaults de um tipo remapeado (netID já é requested_type).
    void (*on_defaults)(Il2CppObject* item, int32_t requested_type, int32_t shell_type);
    // RebuildTooltip de um item com netID fora da faixa original. Nulo: tooltip vazio.
    void (*on_rebuild_tooltip)(Il2CppObject* item, int32_t net_id);
} TLModItemCallbacks;
void TLMod_SetItemCallbacks(const TLModItemCallbacks* callbacks);  // copia; nulo desliga

// Atributos de Terraria.Item que um item de mod define.
typedef struct TLModItemStats {
    int32_t width, height;
    int32_t damage;
    int32_t useTime, useAnimation;
    uint8_t useStyle;  // 0 = não usa; 1 = golpe
    bool melee;
    int32_t maxStack;
    int32_t value;     // em moedas de cobre
    int32_t rare;
} TLModItemStats;
bool TLMod_ReadItemStats(Il2CppObject* item, TLModItemStats* out);
bool TLMod_WriteItemStats(Il2CppObject* item, const TLModItemStats* in);

// Nome exibido (Item._nameOverride: get_Name/AffixName/HoverName usam). Nulo
// limpa. As strings ficam em cache e vivas pelo processo (GC handle).
bool TLMod_SetItemName(Il2CppObject* item, const char* utf8);
// Tooltip exibido: um ItemTooltip cujo LocalizedText tem este texto ('\n'
// separa linhas). Nulo ou vazio: ItemTooltip.None. Em cache, como o nome.
bool TLMod_SetItemTooltip(Il2CppObject* item, const char* utf8);
// Item.SetDefaults(type) pelo caminho do jogo (passa pelo hook). Qualquer
// thread anexada; só mexe neste Item.
bool TLMod_SetItemDefaults(Il2CppObject* item, int32_t type);
// Item.Prefix(prefix): aplica o prefixo como o load do jogo faz.
bool TLMod_ApplyItemPrefix(Il2CppObject* item, int32_t prefix);

// Handlers que os hooks apontam (exportados como TLMod_Hooked_PlayerHurt).
void TLMod_Hooked_ItemSetDefaults(Il2CppObject* self, int32_t type, bool noMatCheck, Il2CppObject* variant,
                                  const MethodInfo* method);
void TLMod_Hooked_ItemRebuildTooltip(Il2CppObject* self, const MethodInfo* method);
void TLMod_Hooked_PlayerInternalSavePlayerFile(Il2CppObject* playerFile, const MethodInfo* method);
Il2CppObject* TLMod_Hooked_PlayerLoadPlayer(Il2CppObject* path, bool cloudSave, const MethodInfo* method);
void TLMod_Hooked_WorldFileInternalSaveWorld(bool useCloudSaving, bool resetTime, const MethodInfo* method);
void TLMod_Hooked_WorldFileLoadWorld(bool loadFromCloud, const MethodInfo* method);
void TLMod_Hooked_MainDoUpdate(Il2CppObject* self, Il2CppObject* gameTime, const MethodInfo* method);
int32_t TLMod_Hooked_NpcNewNPC(Il2CppObject* source, int32_t x, int32_t y, int32_t type,
                                int32_t start, float ai0, float ai1, float ai2, float ai3,
                                int32_t target, const MethodInfo* method);
void TLMod_Hooked_NpcSetDefaults(Il2CppObject* self, int32_t type, NPCSpawnParams spawnparams,
                                  const MethodInfo* method);
void TLMod_Hooked_NpcAI(Il2CppObject* self, const MethodInfo* method);

}  // extern "C"
#pragma GCC visibility pop
