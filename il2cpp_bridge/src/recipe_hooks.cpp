// Receitas de mod em Main.recipe (ver bridge.h, "Receitas de mod").
//
//   Recipe.SetupRecipes()                 hook pós: o jogo já cadastrou as originais
//     └ para cada receita de mod:         como o jogo faz em SetupRecipes
//         currentRecipe.createItem.SetDefaults(resultado)   passa pelo hook de item
//         currentRecipe.requiredItem[i].SetDefaults(ingrediente)
//         currentRecipe.requiredTile[i] = estação
//         Recipe.AddRecipe()              Main.recipe[numRecipes++] = currentRecipe
//     Recipe.CreateRequiredItemQuickLookups()   refeito para todas
//     entradas das receitas de mod: type da casca -> netID
//
// Na thread do jogo (SetupRecipes roda em Main.Initialize; o retorno pelo
// frame, em Main.DoUpdate). O frame do hook só tem PODs; o resto fica em
// funções noinline, e as chamadas ao jogo passam por il2cpp_runtime_invoke
// (uma exceção vira log, não atravessa o nosso código).
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*StaticVoidFn)(const MethodInfo* method);

namespace {

namespace ro = terraria::recipe_off;
namespace io = terraria::item_off;

struct Resolved {
    FieldInfo* numRecipes = nullptr;     // static int Recipe.numRecipes
    FieldInfo* currentRecipe = nullptr;  // static Recipe Recipe.currentRecipe (private)
    FieldInfo* mainRecipe = nullptr;     // static Recipe[] Main.recipe
    const MethodInfo* setupRecipes = nullptr;
    const MethodInfo* addRecipe = nullptr;
    const MethodInfo* quickLookups = nullptr;
    bool ready = false;
};
Resolved r;

void* g_origSetupRecipesSlot = nullptr;

struct ModRecipe {
    TLModRecipe def{};
    std::string label;
};
std::mutex g_mutex;  // protege g_recipes/g_received/g_added
std::vector<ModRecipe> g_recipes;
bool g_received = false;
int32_t g_added = -1;  // -1 = ainda não aplicadas
std::atomic<bool> g_done{false};  // aplicadas (ou nada a aplicar): o frame nem trava o mutex

template <typename T>
T StaticValue(FieldInfo* f) {
    T v{};
    if (f) api.il2cpp_field_static_get_value(f, &v);
    return v;
}

bool CallStatic(const MethodInfo* m, const char* what) {
    Il2CppException* exc = nullptr;
    api.il2cpp_runtime_invoke(m, nullptr, nullptr, &exc);
    if (exc) {
        bridge::LogException(what, exc);
        return false;
    }
    return true;
}

Il2CppObject* ArrayObject(Il2CppArray* a, int32_t i) {
    if (!a || i < 0 || static_cast<uint32_t>(i) >= api.il2cpp_array_length(a)) return nullptr;
    return ArrayData<Il2CppObject*>(a)[i];
}

// Preenche Recipe.currentRecipe com `m`. false = algo do jogo faltou (log feito).
bool FillCurrent(Il2CppObject* current, const ModRecipe& m) {
    const TLModRecipe& d = m.def;
    Il2CppObject* create = FieldAt<Il2CppObject*>(current, ro::createItem);
    Il2CppArray* items = FieldAt<Il2CppArray*>(current, ro::requiredItem);
    Il2CppArray* tiles = FieldAt<Il2CppArray*>(current, ro::requiredTile);
    if (!create || !items || !tiles || static_cast<int32_t>(api.il2cpp_array_length(items)) < d.ingredient_count ||
        static_cast<int32_t>(api.il2cpp_array_length(tiles)) < d.station_count) {
        LOGE("receita %s: Recipe.currentRecipe incompleto", m.label.c_str());
        return false;
    }
    if (!TLMod_SetItemDefaults(create, d.result)) return false;
    FieldAt<int32_t>(create, io::stack) = d.result_stack;
    for (int32_t i = 0; i < d.ingredient_count; ++i) {
        Il2CppObject* item = ArrayObject(items, i);
        if (!item || !TLMod_SetItemDefaults(item, d.ingredients[i])) return false;
        FieldAt<int32_t>(item, io::stack) = d.ingredient_stacks[i];
    }
    for (int32_t i = 0; i < d.station_count; ++i) ArrayData<int32_t>(tiles)[i] = d.stations[i];
    FieldAt<bool>(current, ro::needWater) = d.need_water;
    FieldAt<bool>(current, ro::needLava) = d.need_lava;
    FieldAt<bool>(current, ro::needHoney) = d.need_honey;
    FieldAt<bool>(current, ro::notDecraftable) = true;
    return true;
}

// Nas receitas de mod, a consulta rápida de cada ingrediente de mod passa do
// type da casca para o netID (o que Recipe.CollectItems conta).
int PatchQuickLookup(Il2CppObject* recipe) {
    Il2CppArray* items = FieldAt<Il2CppArray*>(recipe, ro::requiredItem);
    Il2CppArray* entries = FieldAt<Il2CppArray*>(recipe, ro::requiredItemQuickLookup);
    if (!items || !entries) return -1;
    // As entradas seguem os ingredientes não vazios, na ordem (os de mod não têm buracos).
    const uint32_t n = api.il2cpp_array_length(entries);
    uint32_t e = 0;
    int patched = 0;
    for (uint32_t i = 0; i < api.il2cpp_array_length(items) && e < n; ++i) {
        Il2CppObject* item = ArrayData<Il2CppObject*>(items)[i];
        if (!item) continue;
        const int32_t type = FieldAt<int32_t>(item, io::type), net = FieldAt<int32_t>(item, io::netID);
        if (type <= 0) continue;  // ar: sem entrada
        auto* entry = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(ArrayData<uint8_t>(entries)) +
                                                 static_cast<size_t>(e) * terraria::kRecipeEntrySize);
        if (net != type && entry[0] == type) {
            entry[0] = net;
            ++patched;
        }
        ++e;
    }
    return patched;
}

__attribute__((noinline)) void ApplyLocked(bool fromSetup) {
    if (g_added >= 0 || !g_received) return;
    if (g_recipes.empty()) {  // nenhum mod tem receita: nada a mexer no jogo
        g_added = 0;
        g_done.store(true, std::memory_order_release);
        return;
    }
    Il2CppArray* recipes = StaticValue<Il2CppArray*>(r.mainRecipe);
    const int32_t vanilla = StaticValue<int32_t>(r.numRecipes);
    if (!recipes || vanilla <= 0) return;  // SetupRecipes ainda não rodou
    const int32_t capacity = static_cast<int32_t>(api.il2cpp_array_length(recipes));
    std::vector<int32_t> indices;
    std::vector<const ModRecipe*> added;
    for (const ModRecipe& m : g_recipes) {
        const int32_t index = StaticValue<int32_t>(r.numRecipes);
        if (index >= capacity) {
            LOGE("receita %s não cabe: Main.recipe tem %d posições e todas estão em uso", m.label.c_str(), capacity);
            continue;
        }
        Il2CppObject* current = StaticValue<Il2CppObject*>(r.currentRecipe);
        if (!current || !FillCurrent(current, m)) continue;
        if (!CallStatic(r.addRecipe, "Recipe.AddRecipe")) continue;
        indices.push_back(index);
        added.push_back(&m);
    }
    g_added = static_cast<int32_t>(indices.size());
    g_done.store(true, std::memory_order_release);
    if (indices.empty()) {
        LOGW("receitas de mod: nenhuma entrou (%zu pedidas)", g_recipes.size());
        return;
    }
    if (!CallStatic(r.quickLookups, "Recipe.CreateRequiredItemQuickLookups")) return;
    int patched = 0;
    for (int32_t index : indices)
        if (Il2CppObject* recipe = ArrayObject(recipes, index)) patched += std::max(0, PatchQuickLookup(recipe));
    const int32_t total = StaticValue<int32_t>(r.numRecipes);
    LOGI("receitas: %d do jogo, %d de mods (ingredientes de mod pelo netID: %d), sobram %d de %d posições%s", vanilla,
         g_added, patched, capacity - total, capacity, fromSetup ? "" : " (depois do SetupRecipes, no frame)");
    for (size_t i = 0; i < indices.size(); ++i) LOGI("receita %s na posição %d", added[i]->label.c_str(), indices[i]);
}

__attribute__((noinline)) void Apply(bool fromSetup) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ApplyLocked(fromSetup);
}

}  // namespace

namespace bridge {

bool ResolveRecipes() {
    Il2CppClass* recipe = api.il2cpp_class_from_name(Image(), "Terraria", "Recipe");
    if (!recipe) {
        LOGW("Terraria.Recipe não encontrado; receitas de mod desligadas");
        return false;
    }
    r.numRecipes = api.il2cpp_class_get_field_from_name(recipe, "numRecipes");
    r.currentRecipe = api.il2cpp_class_get_field_from_name(recipe, "currentRecipe");
    r.mainRecipe = api.il2cpp_class_get_field_from_name(MainClass(), "recipe");
    r.setupRecipes = api.il2cpp_class_get_method_from_name(recipe, "SetupRecipes", terraria::argc::Recipe_SetupRecipes);
    r.addRecipe = api.il2cpp_class_get_method_from_name(recipe, "AddRecipe", terraria::argc::Recipe_AddRecipe);
    r.quickLookups = api.il2cpp_class_get_method_from_name(recipe, "CreateRequiredItemQuickLookups",
                                                           terraria::argc::Recipe_CreateRequiredItemQuickLookups);
    r.ready = r.numRecipes && r.currentRecipe && r.mainRecipe && r.addRecipe && r.quickLookups;
    if (!r.ready) LOGW("receitas de mod desligadas: Recipe.numRecipes/currentRecipe/AddRecipe ou Main.recipe ausente");
    return r.ready;
}

void InstallRecipeHooks() {
    if (!r.ready || !r.setupRecipes) return;  // sem o hook, o frame aplica
    InstallHook("Recipe.SetupRecipes", r.setupRecipes, terraria::rva::Recipe_SetupRecipes,
                reinterpret_cast<void*>(&TLMod_Hooked_RecipeSetupRecipes), &g_origSetupRecipesSlot);
}

void TickRecipes() {
    if (!r.ready || g_done.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_added >= 0 || !g_received) return;
    }
    Apply(false);
}

}  // namespace bridge

extern "C" {

void TLMod_SetModRecipes(const TLModRecipe* recipes, int32_t count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_received) {
        LOGW("TLMod_SetModRecipes: as receitas de mod já foram entregues; pedido ignorado");
        return;
    }
    g_received = true;
    for (int32_t i = 0; recipes && i < count; ++i) {
        ModRecipe m;
        m.def = recipes[i];
        m.label = recipes[i].label ? recipes[i].label : "?";
        m.def.label = nullptr;
        m.def.ingredient_count = std::min(std::max(m.def.ingredient_count, 0), TLMOD_RECIPE_MAX_INGREDIENTS);
        m.def.station_count = std::min(std::max(m.def.station_count, 0), TLMOD_RECIPE_MAX_STATIONS);
        g_recipes.push_back(std::move(m));
    }
}

int32_t TLMod_ModRecipesAdded(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_added;
}

// Recipe.SetupRecipes (RVA 0x122ECB8), estático: as originais primeiro.
void TLMod_Hooked_RecipeSetupRecipes(const MethodInfo* method) {
    auto orig = reinterpret_cast<StaticVoidFn>(__atomic_load_n(&g_origSetupRecipesSlot, __ATOMIC_ACQUIRE));
    if (orig) orig(method);
    Apply(true);
}

}  // extern "C"
