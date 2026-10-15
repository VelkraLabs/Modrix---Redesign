/*
 * TLMod Mod API — contrato entre um mod e o runtime do TLMod.
 *
 * Este é o ÚNICO header de que um mod precisa. É C puro (ABI estável entre
 * compiladores e versões de libc++), não depende de nenhum outro header do
 * TLMod e não expõe nada do jogo nem do IL2CPP: players são índices,
 * conteúdo é identificado por chaves "mod_id:nome" e IDs numéricos.
 *
 * Um mod é uma pasta com:
 *   manifest.json           metadados, dependências, conteúdo declarado, entry por plataforma
 *   lib/<plataforma>/...    biblioteca nativa que exporta tlmod_mod_init
 *   (assets opcionais)      texturas etc., referenciadas por caminho relativo
 *
 * Ciclo de vida:
 *   1. O runtime lê todos os manifest.json, valida, resolve dependências e
 *      define a ordem de carga (dependências antes; empates por id).
 *   2. Para cada mod, carrega a biblioteca e chama tlmod_mod_init(host, ctx).
 *      É aqui que o mod registra conteúdo e assina eventos.
 *   3. Eventos (ex.: player_hurt) chegam na thread do jogo, na ordem de carga.
 */
#ifndef TLMOD_API_H
#define TLMOD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Versão da ABI. Muda só quando algo incompatível muda; acréscimos no fim de
 * TLModHostApi não mudam a versão (o mod confere struct_size). */
#define TLMOD_API_VERSION 1u

#if defined(_WIN32)
#define TLMOD_MOD_EXPORT __declspec(dllexport)
#else
#define TLMOD_MOD_EXPORT __attribute__((visibility("default")))
#endif

/* Símbolo que toda biblioteca de mod exporta. */
#define TLMOD_ENTRY_SYMBOL "tlmod_mod_init"

/* ------------------------------------------------------------------------ */
/* Tipos básicos                                                             */
/* ------------------------------------------------------------------------ */

/* ID numérico de conteúdo no jogo. Conteúdo de mods recebe IDs numa faixa
 * reservada acima de todos os IDs originais (veja ContentRegistry). Os IDs
 * podem mudar quando o conjunto de mods muda: para salvar, use a chave. */
typedef int32_t TLModId;
#define TLMOD_INVALID_ID ((TLModId)-1)

/* Índice do player no mundo (0..254). -1 = nenhum. */
typedef int32_t TLModPlayer;
#define TLMOD_NO_PLAYER ((TLModPlayer)-1)

/* Contexto do mod: opaco, um por mod, válido enquanto o jogo roda. */
typedef struct TLModContext TLModContext;

typedef enum TLModContentKind {
    TLMOD_CONTENT_ITEM = 0,
    TLMOD_CONTENT_NPC = 1,
    TLMOD_CONTENT_PROJECTILE = 2,
    TLMOD_CONTENT_TILE = 3,
    TLMOD_CONTENT_WALL = 4,
    TLMOD_CONTENT_BUFF = 5,
    TLMOD_CONTENT_KIND_COUNT
} TLModContentKind;

typedef enum TLModLogLevel {
    TLMOD_LOG_DEBUG = 0,
    TLMOD_LOG_INFO = 1,
    TLMOD_LOG_WARN = 2,
    TLMOD_LOG_ERROR = 3
} TLModLogLevel;

/* ------------------------------------------------------------------------ */
/* Definições de conteúdo                                                    */
/* ------------------------------------------------------------------------ */

/* Item. Preencha struct_size = sizeof(TLModItemDef): campos novos serão
 * acrescentados no fim, e o runtime usa struct_size para saber quais existem. */
typedef struct TLModItemDef {
    uint32_t struct_size;
    const char* name;          /* chave local [a-z0-9_]{1,64}; declarada em manifest content.items */
    const char* display_name;  /* nome exibido */
    const char* tooltip;       /* pode ser NULL */
    const char* texture;       /* caminho relativo à pasta do mod; pode ser NULL */
    int32_t width;             /* hitbox em pixels */
    int32_t height;
    int32_t max_stack;         /* 1 = não empilha */
    int32_t value;             /* preço em moedas de cobre */
    int32_t rarity;            /* -1 (cinza) .. 11 */
    int32_t damage;            /* 0 = não é arma; > 0 vira arma de golpe (corpo a corpo) */
    int32_t use_time;          /* em ticks (60 por segundo) */
    /* Acrescentado depois de use_time: o runtime só lê se struct_size o cobre
     * (mods antigos ficam com a casca padrão).
     * Item original usado como "casca": o jogo só vê este tipo (textura,
     * tabelas por tipo), e o runtime aplica por cima os atributos, o nome e o
     * tooltip acima. 0 = padrão (Lente, um material sem uso). Precisa ser um
     * item original (1..ItemID.Count-1). */
    TLModId shell_type;
} TLModItemDef;

/* NPC / boss.
 *
 * Um NPC de mod roda dentro de uma "casca": um NPC original (shell_type) que
 * fornece textura, hitbox, sons e, se o mod quiser, a própria AI. O jogo só
 * enxerga a casca; o runtime sabe quais slots de Main.npc são do mod e chama
 * os callbacks abaixo.
 *
 * Estado:
 *   ai[0..3]  campos do jogo. Se on_ai devolver false, a AI da casca roda e
 *             usa ai[] para as fases DELA: não guarde estado do mod ali nesse caso.
 *   local[]   estado privado do mod por NPC, guardado pelo runtime. O jogo
 *             nunca toca. (Ainda não vai pela rede.)
 */
typedef struct TLModNpcState {
    int32_t index;          /* posição em Main.npc (0..199) */
    TLModId type;          /* ID do NPC de mod (faixa de mods, ex.: 4096) */
    TLModId shell_type;    /* tipo original em uso como casca */
    float x, y;             /* posição, canto superior esquerdo, em pixels */
    float vx, vy;           /* velocidade; o jogo aplica depois da AI */
    int32_t life, life_max, damage, defense;
    TLModPlayer target;
    float ai[4];
    float local[8];
} TLModNpcState;

typedef void (*TLModNpcSpawnFn)(void* user, TLModNpcState* npc);
/* Roda a cada tick (60/s), na thread do jogo, antes da AI da casca.
 * true = o mod assume este tick (a AI da casca não roda);
 * false = a AI da casca roda depois. */
typedef bool (*TLModNpcAiFn)(void* user, TLModNpcState* npc);

typedef struct TLModNpcDef {
    uint32_t struct_size;
    const char* name;          /* chave local; declarada em manifest content.npcs */
    const char* display_name;
    TLModId shell_type;       /* NPC original usado como casca (1..NPCID.Count-1) */
    int32_t life_max;          /* <= 0 mantém o da casca */
    int32_t damage;            /* < 0 mantém o da casca */
    int32_t defense;           /* < 0 mantém o da casca */
    bool boss;                 /* reservado: barra de vida/música de boss */
    void* user;                /* repassado aos callbacks */
    TLModNpcSpawnFn on_spawn; /* opcional: logo após o NPC entrar no mundo */
    TLModNpcAiFn on_ai;       /* opcional: comportamento por tick */
} TLModNpcDef;

/* Progresso do mundo atual. Valores só são acrescentados ao fim (ABI). */
typedef enum TLModProgress {
    TLMOD_PROGRESS_HARDMODE = 0,       /* Wall of Flesh derrotado */
    TLMOD_DOWNED_KING_SLIME,
    TLMOD_DOWNED_EYE_OF_CTHULHU,
    TLMOD_DOWNED_EVIL_BOSS,            /* Devorador de Mundos OU Cérebro de Cthulhu */
    TLMOD_DOWNED_QUEEN_BEE,
    TLMOD_DOWNED_SKELETRON,
    TLMOD_DOWNED_DEERCLOPS,
    TLMOD_DOWNED_QUEEN_SLIME,
    TLMOD_DOWNED_DESTROYER,
    TLMOD_DOWNED_TWINS,
    TLMOD_DOWNED_SKELETRON_PRIME,
    TLMOD_DOWNED_PLANTERA,
    TLMOD_DOWNED_GOLEM,
    TLMOD_DOWNED_DUKE_FISHRON,
    TLMOD_DOWNED_EMPRESS_OF_LIGHT,
    TLMOD_DOWNED_LUNATIC_CULTIST,
    TLMOD_DOWNED_MOON_LORD,
    TLMOD_DOWNED_GOBLIN_ARMY,
    TLMOD_DOWNED_FROST_LEGION,
    TLMOD_DOWNED_PIRATES,
    TLMOD_DOWNED_MARTIANS,
    TLMOD_DOWNED_MOURNING_WOOD,
    TLMOD_DOWNED_PUMPKING,
    TLMOD_DOWNED_EVERSCREAM,
    TLMOD_DOWNED_SANTA_NK1,
    TLMOD_DOWNED_ICE_QUEEN,
    TLMOD_DOWNED_SOLAR_PILLAR,
    TLMOD_DOWNED_VORTEX_PILLAR,
    TLMOD_DOWNED_NEBULA_PILLAR,
    TLMOD_DOWNED_STARDUST_PILLAR,
    TLMOD_PROGRESS_COUNT
} TLModProgress;

/* Provedor de dados para a camada de interface. O mod escreve um documento
 * JSON em `out` com host->buffer_append; o runtime valida o JSON antes de
 * entregar à interface. Pode ser chamado em QUALQUER thread (a da UI): use
 * só funções seguras para isso (progress, in_world, find_content, log). */
typedef struct TLModBuffer TLModBuffer;
typedef bool (*TLModProviderFn)(void* user, TLModBuffer* out);

/* ------------------------------------------------------------------------ */
/* Eventos                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct TLModPlayerHurt {
    TLModPlayer player;
    int32_t damage;       /* na fase "pré" o mod pode alterar */
    bool crit;
    bool pvp;
    int32_t life_before;
    int32_t life_after;   /* só preenchido na fase "pós" */
} TLModPlayerHurt;

/* Fase pré (after=false): retornar false cancela o golpe.
 * Fase pós (after=true): só observação; o retorno é ignorado. */
typedef bool (*TLModPlayerHurtFn)(void* user, TLModPlayerHurt* ev, bool after);

/* ------------------------------------------------------------------------ */
/* Tabela de funções que o runtime entrega ao mod                            */
/* ------------------------------------------------------------------------ */

typedef struct TLModHostApi {
    uint32_t api_version;  /* TLMOD_API_VERSION do runtime */
    uint32_t struct_size;  /* sizeof(TLModHostApi) do runtime */

    /* Identidade e log */
    const char* (*mod_id)(const TLModContext* ctx);
    const char* (*mod_version)(const TLModContext* ctx);
    const char* (*mod_dir)(const TLModContext* ctx); /* pasta do mod (com assets) */
    void (*log)(const TLModContext* ctx, TLModLogLevel level, const char* message);

    /* Conteúdo. Só pode ser chamado durante tlmod_mod_init.
     * register_item devolve o ID atribuído ou TLMOD_INVALID_ID (motivo no log). */
    TLModId (*register_item)(TLModContext* ctx, const TLModItemDef* def);
    /* key = "mod_id:nome". Devolve TLMOD_INVALID_ID se não existir. */
    TLModId (*find_content)(const TLModContext* ctx, TLModContentKind kind, const char* key);

    /* Jogo */
    bool (*subscribe_player_hurt)(TLModContext* ctx, TLModPlayerHurtFn fn, void* user);
    TLModPlayer (*local_player)(const TLModContext* ctx);
    int32_t (*player_life)(const TLModContext* ctx, TLModPlayer player); /* -1 se inválido */

    /* NPCs. Campos acrescentados ao fim da tabela: um mod que os usa deve
     * conferir struct_size (tlmod.hpp já confere). */
    TLModId (*register_npc)(TLModContext* ctx, const TLModNpcDef* def); /* só no init */
    /* Cria um NPC (original ou de mod) via NPC.NewNPC. Só na thread do jogo
     * (ex.: dentro de on_ai ou de um evento). Devolve o índice ou -1. */
    int32_t (*spawn_npc)(TLModContext* ctx, TLModId type, float x, float y);

    /* Estado do mundo. Seguras em qualquer thread: leem uma fotografia que o
     * runtime atualiza a cada frame na thread do jogo.
     * Retornam 1 (sim), 0 (não) ou -1 (desconhecido: sem mundo carregado,
     * jogo ainda não rodou um frame, ou flag não mapeada nesta versão). */
    int32_t (*in_world)(const TLModContext* ctx);
    int32_t (*progress)(const TLModContext* ctx, TLModProgress flag);

    /* Canal de dados para a interface ("mod_id/channel"). Só durante o init;
     * channel [a-z0-9_]{1,64}, declarado em manifest "provides". */
    bool (*register_provider)(TLModContext* ctx, const char* channel, TLModProviderFn fn, void* user);
    void (*buffer_append)(TLModBuffer* out, const char* data, size_t len);

    /* Dá `stack` unidades do item `type` (original ou de mod) ao jogador
     * local. Pode ser chamado de qualquer thread, inclusive no init: a entrega
     * acontece na thread do jogo, no próximo frame com um mundo carregado,
     * empilhando com o que ele já tem e depois nos slots livres do inventário.
     * false = tipo desconhecido ou stack < 1. O resultado da entrega vai para o log. */
    bool (*give_item)(TLModContext* ctx, TLModId type, int32_t stack);
} TLModHostApi;

/* Assinatura do ponto de entrada. Retorne 0 em sucesso; qualquer outro valor
 * marca o mod como falho e os mods que dependem dele não são carregados. */
typedef int (*TLModModInitFn)(const TLModHostApi* host, TLModContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* TLMOD_API_H */
