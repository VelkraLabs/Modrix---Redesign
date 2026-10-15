#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "tlmod/runtime/content_registry.h"

namespace tlmod::rt {

// O que um item de mod herda da casca: tudo que a definição não sobrescreve.
// Os primeiros vêm de ItemID.Sets[type] (arrays estáticos por type); os outros,
// do SetDefaults da casca (campos do Item). Ver "Escolhendo a casca" no README.
struct ItemTraits {
    // ItemID.Sets
    bool shouldNotBeInInventory = false;  // ItemsThatShouldNotBeInInventory: some do inventário
    bool isAPickup = false;               // coração/estrela: coletado ao encostar, nunca no inventário
    bool nebulaPickup = false;
    bool commonCoin = false;              // moeda: junta com o dinheiro
    bool bossBag = false, openableBag = false, fishingCrate = false;  // botão direito abre e solta loot
    bool deprecated = false;
    bool isFood = false;
    bool canGetPrefixes = true;           // aceita prefixo (reforja)
    bool isAMaterial = false;             // linha "Material" no tooltip
    bool isAKite = false, yoyo = false, isDrill = false, isChainsaw = false, gunProj = false;
    bool summonerScalesWithAttackSpeed = false;
    int32_t shimmerTransformToItem = -1;  // no Shimmer vira este item
    int32_t extractinatorMode = -1;       // o Extrator aceita
    int32_t staffMinionSlotsRequired = 0;
    float tooltipDamageMultiplier = 1.0f;
    // Item depois do SetDefaults da casca
    int32_t damage = 0, useStyle = 0, maxStack = 1, shoot = 0, ammo = 0, useAmmo = 0, mana = 0;
    int32_t createTile = -1, createWall = -1, pick = 0, axe = 0, hammer = 0, defense = 0;
    int32_t headSlot = -1, bodySlot = -1, legSlot = -1, buffType = 0, healLife = 0, healMana = 0;
    int32_t bait = 0, fishingPole = 0, makeNpc = 0, mountType = -1;
    bool accessory = false, consumable = false, potion = false, vanity = false, channel = false;
    bool melee = false, ranged = false, magic = false, summon = false, sentry = false;
};

// Categorias para escolher a casca (a definição não diz a categoria: ela é
// deduzida do dano da definição e do que a casca é).
enum class ItemCategory { Material, MeleeWeapon, Bow, Gun, Ammo, Accessory };
const char* ToString(ItemCategory c);

struct ShellRecommendation {
    ItemCategory category;
    TLModId shell;
    const char* shellName;
    const char* why;
};
// Uma por categoria (README: "Escolhendo a casca").
const std::array<ShellRecommendation, 6>& RecommendedShells();
const ShellRecommendation& RecommendedShell(ItemCategory c);

ItemCategory InferCategory(const ItemDefinition& def, const ItemTraits& shell);

// Propriedades da casca que provavelmente não combinam com a definição.
struct ShellIssue {
    std::string text;
};
std::vector<ShellIssue> CheckShell(const ItemDefinition& def, const ItemTraits& shell);

}  // namespace tlmod::rt
