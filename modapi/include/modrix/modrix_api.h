/*
 * Modrix Mod API — contrato entre um mod e o runtime do Modrix.
 *
 * Este é o ÚNICO header de que um mod precisa. É C puro (ABI estável entre
 * compiladores e versões de libc++), não depende de nenhum outro header do
 * Modrix e não expõe nada do jogo nem do IL2CPP: players são índices,
 * conteúdo é identificado por chaves "mod_id:nome" e IDs numéricos.
 *
 * Um mod é uma pasta com:
 *   manifest.json           metadados, dependências, conteúdo declarado, entry por plataforma
 *   lib/<plataforma>/...    biblioteca nativa que exporta modrix_mod_init
 *   (assets opcionais)      texturas etc., referenciadas por caminho relativo
 *
 * Ciclo de vida:
 *   1. O runtime lê todos os manifest.json, valida, resolve dependências e
 *      define a ordem de carga (dependências antes; empates por id).
 *   2. Para cada mod, carrega a biblioteca e chama modrix_mod_init(host, ctx).
 *      É aqui que o mod registra conteúdo e assina eventos.
 *   3. Eventos (ex.: player_hurt) chegam na thread do jogo, na ordem de carga.
 */
#ifndef MODRIX_API_H
#define MODRIX_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Versão da ABI. Muda só quando algo incompatível muda; acréscimos no fim de
 * ModrixHostApi não mudam a versão (o mod confere struct_size). */
#define MODRIX_API_VERSION 1u

#if defined(_WIN32)
#define MODRIX_MOD_EXPORT __declspec(dllexport)
#else
#define MODRIX_MOD_EXPORT __attribute__((visibility("default")))
#endif

/* Símbolo que toda biblioteca de mod exporta. */
#define MODRIX_ENTRY_SYMBOL "modrix_mod_init"

/* ------------------------------------------------------------------------ */
/* Tipos básicos                                                             */
/* ------------------------------------------------------------------------ */

/* ID numérico de conteúdo no jogo. Conteúdo de mods recebe IDs numa faixa
 * reservada acima de todos os IDs originais (veja ContentRegistry). Os IDs
 * podem mudar quando o conjunto de mods muda: para salvar, use a chave. */
typedef int32_t ModrixId;
#define MODRIX_INVALID_ID ((ModrixId)-1)

/* Índice do player no mundo (0..254). -1 = nenhum. */
typedef int32_t ModrixPlayer;
#define MODRIX_NO_PLAYER ((ModrixPlayer)-1)

/* Contexto do mod: opaco, um por mod, válido enquanto o jogo roda. */
typedef struct ModrixContext ModrixContext;

typedef enum ModrixContentKind {
    MODRIX_CONTENT_ITEM = 0,
    MODRIX_CONTENT_NPC = 1,
    MODRIX_CONTENT_PROJECTILE = 2,
    MODRIX_CONTENT_TILE = 3,
    MODRIX_CONTENT_WALL = 4,
    MODRIX_CONTENT_BUFF = 5,
    MODRIX_CONTENT_KIND_COUNT
} ModrixContentKind;

typedef enum ModrixLogLevel {
    MODRIX_LOG_DEBUG = 0,
    MODRIX_LOG_INFO = 1,
    MODRIX_LOG_WARN = 2,
    MODRIX_LOG_ERROR = 3
} ModrixLogLevel;

/* ------------------------------------------------------------------------ */
/* Definições de conteúdo                                                    */
/* ------------------------------------------------------------------------ */

/* Item. Preencha struct_size = sizeof(ModrixItemDef): campos novos serão
 * acrescentados no fim, e o runtime usa struct_size para saber quais existem. */
typedef struct ModrixItemDef {
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
    int32_t damage;            /* 0 = não é arma */
    int32_t use_time;          /* em ticks (60 por segundo) */
} ModrixItemDef;

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
typedef struct ModrixNpcState {
    int32_t index;          /* posição em Main.npc (0..199) */
    ModrixId type;          /* ID do NPC de mod (faixa de mods, ex.: 4096) */
    ModrixId shell_type;    /* tipo original em uso como casca */
    float x, y;             /* posição, canto superior esquerdo, em pixels */
    float vx, vy;           /* velocidade; o jogo aplica depois da AI */
    int32_t life, life_max, damage, defense;
    ModrixPlayer target;
    float ai[4];
    float local[8];
} ModrixNpcState;

typedef void (*ModrixNpcSpawnFn)(void* user, ModrixNpcState* npc);
/* Roda a cada tick (60/s), na thread do jogo, antes da AI da casca.
 * true = o mod assume este tick (a AI da casca não roda);
 * false = a AI da casca roda depois. */
typedef bool (*ModrixNpcAiFn)(void* user, ModrixNpcState* npc);

typedef struct ModrixNpcDef {
    uint32_t struct_size;
    const char* name;          /* chave local; declarada em manifest content.npcs */
    const char* display_name;
    ModrixId shell_type;       /* NPC original usado como casca (1..NPCID.Count-1) */
    int32_t life_max;          /* <= 0 mantém o da casca */
    int32_t damage;            /* < 0 mantém o da casca */
    int32_t defense;           /* < 0 mantém o da casca */
    bool boss;                 /* reservado: barra de vida/música de boss */
    void* user;                /* repassado aos callbacks */
    ModrixNpcSpawnFn on_spawn; /* opcional: logo após o NPC entrar no mundo */
    ModrixNpcAiFn on_ai;       /* opcional: comportamento por tick */
} ModrixNpcDef;

/* ------------------------------------------------------------------------ */
/* Eventos                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct ModrixPlayerHurt {
    ModrixPlayer player;
    int32_t damage;       /* na fase "pré" o mod pode alterar */
    bool crit;
    bool pvp;
    int32_t life_before;
    int32_t life_after;   /* só preenchido na fase "pós" */
} ModrixPlayerHurt;

/* Fase pré (after=false): retornar false cancela o golpe.
 * Fase pós (after=true): só observação; o retorno é ignorado. */
typedef bool (*ModrixPlayerHurtFn)(void* user, ModrixPlayerHurt* ev, bool after);

/* ------------------------------------------------------------------------ */
/* Tabela de funções que o runtime entrega ao mod                            */
/* ------------------------------------------------------------------------ */

typedef struct ModrixHostApi {
    uint32_t api_version;  /* MODRIX_API_VERSION do runtime */
    uint32_t struct_size;  /* sizeof(ModrixHostApi) do runtime */

    /* Identidade e log */
    const char* (*mod_id)(const ModrixContext* ctx);
    const char* (*mod_version)(const ModrixContext* ctx);
    const char* (*mod_dir)(const ModrixContext* ctx); /* pasta do mod (com assets) */
    void (*log)(const ModrixContext* ctx, ModrixLogLevel level, const char* message);

    /* Conteúdo. Só pode ser chamado durante modrix_mod_init.
     * register_item devolve o ID atribuído ou MODRIX_INVALID_ID (motivo no log). */
    ModrixId (*register_item)(ModrixContext* ctx, const ModrixItemDef* def);
    /* key = "mod_id:nome". Devolve MODRIX_INVALID_ID se não existir. */
    ModrixId (*find_content)(const ModrixContext* ctx, ModrixContentKind kind, const char* key);

    /* Jogo */
    bool (*subscribe_player_hurt)(ModrixContext* ctx, ModrixPlayerHurtFn fn, void* user);
    ModrixPlayer (*local_player)(const ModrixContext* ctx);
    int32_t (*player_life)(const ModrixContext* ctx, ModrixPlayer player); /* -1 se inválido */

    /* NPCs. Campos acrescentados ao fim da tabela: um mod que os usa deve
     * conferir struct_size (modrix.hpp já confere). */
    ModrixId (*register_npc)(ModrixContext* ctx, const ModrixNpcDef* def); /* só no init */
    /* Cria um NPC (original ou de mod) via NPC.NewNPC. Só na thread do jogo
     * (ex.: dentro de on_ai ou de um evento). Devolve o índice ou -1. */
    int32_t (*spawn_npc)(ModrixContext* ctx, ModrixId type, float x, float y);
} ModrixHostApi;

/* Assinatura do ponto de entrada. Retorne 0 em sucesso; qualquer outro valor
 * marca o mod como falho e os mods que dependem dele não são carregados. */
typedef int (*ModrixModInitFn)(const ModrixHostApi* host, ModrixContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* MODRIX_API_H */
