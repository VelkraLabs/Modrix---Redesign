#include "tlmod/runtime/item_shell.h"

namespace tlmod::rt {

const char* ToString(ItemCategory c) {
    switch (c) {
        case ItemCategory::Material: return "material";
        case ItemCategory::MeleeWeapon: return "arma corpo a corpo";
        case ItemCategory::Bow: return "arco";
        case ItemCategory::Gun: return "arma de fogo";
        case ItemCategory::Ammo: return "munição";
        case ItemCategory::Accessory: return "acessório";
    }
    return "?";
}

const std::array<ShellRecommendation, 6>& RecommendedShells() {
    // IDs do dump (ItemID). Escolhidos por não terem nada além do básico da
    // categoria: nenhum ItemID.Sets que mude o comportamento, sem tile, sem buff.
    static const std::array<ShellRecommendation, 6> kShells = {{
        {ItemCategory::Material, 38, "Lente",
         "material sem uso, sem tile e sem tiro; já tem a linha \"Material\" no tooltip"},
        {ItemCategory::MeleeWeapon, 24, "Espada de Madeira",
         "golpe corpo a corpo simples, sem projétil, aceita prefixos de arma corpo a corpo"},
        {ItemCategory::Bow, 39, "Arco de Madeira",
         "consome flechas (useAmmo = Flecha) e dispara a flecha usada, aceita prefixos de arma à distância"},
        {ItemCategory::Gun, 95, "Pistola de Pederneira", "consome balas (useAmmo = Bala) e dispara a bala usada"},
        {ItemCategory::Ammo, 40, "Flecha de Madeira",
         "munição de flecha (ammo = Flecha), consumível, pilha grande; para balas, Bala de Mosquete (97)"},
        {ItemCategory::Accessory, 216, "Algema",
         "acessório sem efeito especial no código (nenhum caso por type em Player.ApplyEquipFunctional); só +1 de "
         "defesa, do campo defense"},
    }};
    return kShells;
}

const ShellRecommendation& RecommendedShell(ItemCategory c) {
    for (const ShellRecommendation& r : RecommendedShells())
        if (r.category == c) return r;
    return RecommendedShells()[0];
}

ItemCategory InferCategory(const ItemDefinition& def, const ItemTraits& shell) {
    if (def.damage > 0) {
        if (shell.ammo > 0) return ItemCategory::Ammo;
        if (shell.useAmmo == 40) return ItemCategory::Bow;  // AmmoID.Arrow
        if (shell.useAmmo == 97) return ItemCategory::Gun;  // AmmoID.Bullet
        return ItemCategory::MeleeWeapon;
    }
    if (shell.accessory) return ItemCategory::Accessory;
    if (shell.ammo > 0) return ItemCategory::Ammo;
    return ItemCategory::Material;
}

std::vector<ShellIssue> CheckShell(const ItemDefinition& def, const ItemTraits& t) {
    std::vector<ShellIssue> out;
    auto add = [&](std::string text) { out.push_back({std::move(text)}); };
    const bool weapon = def.damage > 0;
    const ItemCategory cat = InferCategory(def, t);

    // Valem para qualquer definição: o jogo trata o item pelo type da casca.
    if (t.shouldNotBeInInventory) add("some do inventário (ItemID.Sets.ItemsThatShouldNotBeInInventory)");
    if (t.isAPickup || t.nebulaPickup)
        add("é coletada ao encostar, como coração/estrela, e nunca entra no inventário (ItemID.Sets.IsAPickup)");
    if (t.commonCoin) add("é moeda: junta com o dinheiro do jogador (ItemID.Sets.CommonCoin)");
    if (t.bossBag || t.openableBag || t.fishingCrate)
        add("abre com o botão direito e solta o loot da casca (ItemID.Sets.BossBag/OpenableBag/IsFishingCrate)");
    if (t.deprecated) add("está marcada como obsoleta (ItemID.Sets.Deprecated)");
    if (t.shimmerTransformToItem > 0)
        add("no Shimmer vira o item " + std::to_string(t.shimmerTransformToItem) +
            " (ItemID.Sets.ShimmerTransformToItem)");
    if (t.extractinatorMode >= 0) add("o Extrator aceita e troca por loot (ItemID.Sets.ExtractinatorMode)");
    if (t.createTile >= 0) add("usar coloca o bloco " + std::to_string(t.createTile) + " da casca (createTile)");
    if (t.createWall > 0) add("usar coloca a parede " + std::to_string(t.createWall) + " da casca (createWall)");
    if (t.makeNpc > 0) add("usar solta o NPC " + std::to_string(t.makeNpc) + " da casca (makeNPC)");
    if (t.mountType >= 0) add("usar invoca a montaria da casca (mountType)");
    if (t.buffType > 0 || t.healLife > 0 || t.healMana > 0 || t.potion)
        add("usar aplica o efeito da casca (buff " + std::to_string(t.buffType) + ", cura " +
            std::to_string(t.healLife) + "/" + std::to_string(t.healMana) + ")");
    if (t.headSlot >= 0 || t.bodySlot >= 0 || t.legSlot >= 0)
        add("é armadura: equipa com o visual e o bônus de conjunto da casca");
    if (t.fishingPole > 0 || t.bait > 0) add("é vara de pesca ou isca (fishingPole/bait)");
    if (t.pick > 0 || t.axe > 0 || t.hammer > 0) add("também é ferramenta (pick/axe/hammer da casca)");

    if (weapon) {
        if (!t.canGetPrefixes && cat != ItemCategory::Ammo) add("não aceita prefixos: não dá para reforjar (ItemID.Sets.CanGetPrefixes)");
        if (t.tooltipDamageMultiplier != 1.0f)
            add("o tooltip mostra o dano multiplicado por " + std::to_string(t.tooltipDamageMultiplier) +
                " (ItemID.Sets.ToolTipDamageMultiplier)");
        if (t.consumable && def.maxStack == 1)
            add("a casca é consumida ao usar; com max_stack 1 o item some no primeiro uso");
        if (t.ammo > 0 && def.maxStack == 1) add("a casca é munição, mas max_stack é 1");
        if (t.magic || t.mana > 0) add("gasta " + std::to_string(t.mana) + " de mana por uso (magic)");
        if (t.summon || t.sentry || t.staffMinionSlotsRequired > 0)
            add("invoca o lacaio/sentinela da casca (summon/sentry)");
        if (t.channel || t.yoyo || t.isAKite || t.isDrill || t.isChainsaw)
            add("tem uso especial da casca (canalizado, ioiô, pipa, broca ou motosserra)");
        if (t.accessory) add("é acessório e arma ao mesmo tempo");
    } else {
        if (t.damage > 0) add("a casca é arma (dano " + std::to_string(t.damage) + "): continua golpeando/atirando, com dano 0");
        else if (t.useStyle != 0 && !t.consumable) add("a casca é usável (useStyle " + std::to_string(t.useStyle) + ")");
        if (t.shoot > 0 && t.ammo == 0) add("usar dispara o projétil " + std::to_string(t.shoot) + " da casca (shoot)");
        if (t.ammo > 0 && cat != ItemCategory::Ammo) add("conta como munição: armas da casca consomem este item");
        if (t.ammo > 0 && def.maxStack == 1) add("a casca é munição, mas max_stack é 1");
        if (t.consumable && t.useStyle != 0 && t.ammo == 0) add("usar gasta uma unidade (consumable)");
        if (t.accessory && def.maxStack > 1) add("acessório com max_stack > 1: acessórios não empilham no jogo");
    }
    return out;
}

}  // namespace tlmod::rt
