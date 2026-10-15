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
} ModrixHostApi;

/* Assinatura do ponto de entrada. Retorne 0 em sucesso; qualquer outro valor
 * marca o mod como falho e os mods que dependem dele não são carregados. */
typedef int (*ModrixModInitFn)(const ModrixHostApi* host, ModrixContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* MODRIX_API_H */
