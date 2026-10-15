// Propriedades de um item original, para avaliar a casca de um item de mod:
// os arrays de ItemID.Sets (por type) e os campos que o SetDefaults preenche.
// Ver TLMod_ReadItemTraits em bridge.h.
#include <cstring>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

namespace {

namespace io = terraria::item_off;

struct TraitsResolved {
    bool ready = false;
    Il2CppClass* item = nullptr;
    const MethodInfo* itemCtor = nullptr;  // Item..ctor()
    uint32_t probe = 0;                    // GC handle do Item de sonda
    Il2CppClass* sets = nullptr;           // Terraria.ID.ItemID.Sets
    // Offsets de instância do Item (dump; FieldOffset troca pelos do runtime).
    size_t damage = io::damage, useStyle = io::useStyle, maxStack = io::maxStack, shoot = io::shoot,
           ammo = io::ammo, useAmmo = io::useAmmo, mana = io::mana, createTile = io::createTile,
           createWall = io::createWall, pick = io::pick, axe = io::axe, hammer = io::hammer, defense = io::defense,
           headSlot = io::headSlot, bodySlot = io::bodySlot, legSlot = io::legSlot, buffType = io::buffType,
           healLife = io::healLife, healMana = io::healMana, bait = io::bait, fishingPole = io::fishingPole,
           makeNPC = io::makeNPC, mountType = io::mountType, accessory = io::accessory, consumable = io::consumable,
           potion = io::potion, vanity = io::vanity, channel = io::channel, melee = io::melee, ranged = io::ranged,
           magic = io::magic, summon = io::summon, sentry = io::sentry;
};
TraitsResolved t;

// Aninhada: o IL2CPP não acha "ItemID.Sets" por nome; percorre as aninhadas.
Il2CppClass* NestedClass(Il2CppClass* outer, const char* name) {
    void* iter = nullptr;
    while (Il2CppClass* k = api.il2cpp_class_get_nested_types(outer, &iter))
        if (std::strcmp(api.il2cpp_class_get_name(k), name) == 0) return k;
    return nullptr;
}

Il2CppArray* SetArray(const char* name) {
    FieldInfo* f = t.sets ? api.il2cpp_class_get_field_from_name(t.sets, name) : nullptr;
    Il2CppArray* arr = nullptr;
    if (f) api.il2cpp_field_static_get_value(f, &arr);
    return arr;
}

template <typename T>
T SetValue(const char* name, int32_t type, T fallback) {
    Il2CppArray* arr = SetArray(name);
    if (!arr || type < 0 || static_cast<uint32_t>(type) >= api.il2cpp_array_length(arr)) return fallback;
    return ArrayData<T>(arr)[type];
}

Il2CppObject* Probe() {
    if (t.probe) return api.il2cpp_gchandle_get_target(t.probe);
    Il2CppObject* item = api.il2cpp_object_new(t.item);
    if (!item) return nullptr;
    if (t.itemCtor) {
        Il2CppException* exc = nullptr;
        api.il2cpp_runtime_invoke(t.itemCtor, item, nullptr, &exc);
        if (exc) {
            bridge::LogException("Item..ctor (sonda)", exc);
            return nullptr;
        }
    }
    t.probe = api.il2cpp_gchandle_new(item, false);
    return item;
}

}  // namespace

namespace bridge {

bool ResolveItemTraits() {
    t.item = api.il2cpp_class_from_name(Image(), terraria::kNamespace, terraria::kItemClass);
    Il2CppClass* itemId = api.il2cpp_class_from_name(Image(), "Terraria.ID", terraria::kItemIdClass);
    t.sets = itemId ? NestedClass(itemId, terraria::kItemIdSetsClass) : nullptr;
    if (!t.item || !t.sets) {
        LOGW("propriedades de casca: Item=%p ItemID.Sets=%p; avaliação de casca desativada",
             static_cast<void*>(t.item), static_cast<void*>(t.sets));
        return false;
    }
    t.itemCtor = api.il2cpp_class_get_method_from_name(t.item, ".ctor", 0);
    struct Off {
        size_t* dst;
        const char* name;
    };
    const Off offs[] = {{&t.damage, "damage"},       {&t.useStyle, "useStyle"},     {&t.maxStack, "maxStack"},
                        {&t.shoot, "shoot"},         {&t.ammo, "ammo"},             {&t.useAmmo, "useAmmo"},
                        {&t.mana, "mana"},           {&t.createTile, "createTile"}, {&t.createWall, "createWall"},
                        {&t.pick, "pick"},           {&t.axe, "axe"},               {&t.hammer, "hammer"},
                        {&t.defense, "defense"},     {&t.headSlot, "headSlot"},     {&t.bodySlot, "bodySlot"},
                        {&t.legSlot, "legSlot"},     {&t.buffType, "buffType"},     {&t.healLife, "healLife"},
                        {&t.healMana, "healMana"},   {&t.bait, "bait"},             {&t.fishingPole, "fishingPole"},
                        {&t.makeNPC, "makeNPC"},     {&t.mountType, "mountType"},   {&t.accessory, "accessory"},
                        {&t.consumable, "consumable"}, {&t.potion, "potion"},       {&t.vanity, "vanity"},
                        {&t.channel, "channel"},     {&t.melee, "melee"},           {&t.ranged, "ranged"},
                        {&t.magic, "magic"},         {&t.summon, "summon"},         {&t.sentry, "sentry"}};
    for (const Off& o : offs) *o.dst = FieldOffset(t.item, o.name, *o.dst);
    t.ready = true;
    return true;
}

}  // namespace bridge

extern "C" bool TLMod_ReadItemTraits(int32_t type, TLModItemTraits* out) {
    if (!out || !t.ready || type <= 0 || type >= terraria::kItemVanillaCount || !bridge::OnGameThread()) return false;
    // Os arrays existem depois do .cctor de ItemID.Sets; antes disso, "ainda não".
    if (!SetArray("CanGetPrefixes")) return false;
    Il2CppObject* probe = Probe();
    if (!probe || !TLMod_SetItemDefaults(probe, type)) return false;

    TLModItemTraits r{};
    r.should_not_be_in_inventory = SetValue<bool>("ItemsThatShouldNotBeInInventory", type, false);
    r.is_a_pickup = SetValue<bool>("IsAPickup", type, false);
    r.nebula_pickup = SetValue<bool>("NebulaPickup", type, false);
    r.common_coin = SetValue<bool>("CommonCoin", type, false);
    r.boss_bag = SetValue<bool>("BossBag", type, false);
    r.openable_bag = SetValue<bool>("OpenableBag", type, false);
    r.fishing_crate = SetValue<bool>("IsFishingCrate", type, false);
    r.deprecated = SetValue<bool>("Deprecated", type, false);
    r.is_food = SetValue<bool>("IsFood", type, false);
    r.can_get_prefixes = SetValue<bool>("CanGetPrefixes", type, true);
    r.is_a_material = SetValue<bool>("IsAMaterial", type, false);
    r.is_a_kite = SetValue<bool>("IsAKite", type, false);
    r.yoyo = SetValue<bool>("Yoyo", type, false);
    r.is_drill = SetValue<bool>("IsDrill", type, false);
    r.is_chainsaw = SetValue<bool>("IsChainsaw", type, false);
    r.gun_proj = SetValue<bool>("gunProj", type, false);
    r.summoner_scales_with_attack_speed = SetValue<bool>("SummonerWeaponThatScalesWithAttackSpeed", type, false);
    r.shimmer_transform_to_item = SetValue<int32_t>("ShimmerTransformToItem", type, -1);
    r.extractinator_mode = SetValue<int32_t>("ExtractinatorMode", type, -1);
    r.staff_minion_slots_required = SetValue<int32_t>("StaffMinionSlotsRequired", type, 0);
    r.tooltip_damage_multiplier = SetValue<float>("ToolTipDamageMultiplier", type, 1.0f);

    r.damage = FieldAt<int32_t>(probe, t.damage);
    r.use_style = FieldAt<uint8_t>(probe, t.useStyle);
    r.max_stack = FieldAt<int32_t>(probe, t.maxStack);
    r.shoot = FieldAt<int32_t>(probe, t.shoot);
    r.ammo = FieldAt<int32_t>(probe, t.ammo);
    r.use_ammo = FieldAt<int32_t>(probe, t.useAmmo);
    r.mana = FieldAt<int32_t>(probe, t.mana);
    r.create_tile = FieldAt<int32_t>(probe, t.createTile);
    r.create_wall = FieldAt<int32_t>(probe, t.createWall);
    r.pick = FieldAt<int32_t>(probe, t.pick);
    r.axe = FieldAt<int32_t>(probe, t.axe);
    r.hammer = FieldAt<int32_t>(probe, t.hammer);
    r.defense = FieldAt<int32_t>(probe, t.defense);
    r.head_slot = FieldAt<int32_t>(probe, t.headSlot);
    r.body_slot = FieldAt<int32_t>(probe, t.bodySlot);
    r.leg_slot = FieldAt<int32_t>(probe, t.legSlot);
    r.buff_type = FieldAt<int32_t>(probe, t.buffType);
    r.heal_life = FieldAt<int32_t>(probe, t.healLife);
    r.heal_mana = FieldAt<int32_t>(probe, t.healMana);
    r.bait = FieldAt<int32_t>(probe, t.bait);
    r.fishing_pole = FieldAt<int32_t>(probe, t.fishingPole);
    r.make_npc = FieldAt<int16_t>(probe, t.makeNPC);
    r.mount_type = FieldAt<int32_t>(probe, t.mountType);
    r.accessory = FieldAt<bool>(probe, t.accessory);
    r.consumable = FieldAt<bool>(probe, t.consumable);
    r.potion = FieldAt<bool>(probe, t.potion);
    r.vanity = FieldAt<bool>(probe, t.vanity);
    r.channel = FieldAt<bool>(probe, t.channel);
    r.melee = FieldAt<bool>(probe, t.melee);
    r.ranged = FieldAt<bool>(probe, t.ranged);
    r.magic = FieldAt<bool>(probe, t.magic);
    r.summon = FieldAt<bool>(probe, t.summon);
    r.sentry = FieldAt<bool>(probe, t.sentry);
    *out = r;
    return true;
}
