// Texturas de mod: criação pelo Unity e troca do Asset durante o desenho de um
// item/NPC de mod. Ver o comentário em bridge.h e terraria_offsets.h.
#include <atomic>
#include <cstring>
#include <initializer_list>

#include "bridge.h"
#include "bridge_internal.h"
#include "log.h"
#include "terraria_offsets.h"

using namespace il2cpp;

typedef void (*GridDrawItemFn)(Il2CppObject* self, Il2CppObject* spriteBatch, Il2CppObject* layout, Il2CppArray* inv,
                               int32_t slot, TLModXnaVector2 position, TLModXnaColor lightColor, float scale,
                               const MethodInfo* method);
typedef void (*MainDrawItemFn)(Il2CppObject* self, Il2CppObject* item, int32_t whoami, const MethodInfo* method);
typedef float (*DrawItemIconFn)(Il2CppObject* item, int32_t context, Il2CppObject* spriteBatch, TLModXnaVector2 pos,
                                float scale, float sizeLimit, TLModXnaColor environmentColor, const MethodInfo* method);
typedef void (*HeldItemFn)(void* drawinfo, const MethodInfo* method);
typedef void (*DrawNpcDirectFn)(Il2CppObject* self, Il2CppObject* spriteBatch, Il2CppObject* npc, bool behindTiles,
                                TLModXnaVector2 screenPos, Il2CppObject* lightMap, TLModXnaRectangle* lightRegion,
                                const MethodInfo* method);

namespace {

// Cabeçalho de objeto (klass + monitor): somado ao offset de campos de struct.
constexpr size_t kObjectHeader = 2 * sizeof(void*);

struct DrawResolved {
    bool ready = false;  // hooks de desenho
    // Item / NPC (começam no dump; FieldOffset troca pelos do runtime).
    size_t itemType = terraria::item_off::type;
    size_t itemNetId = terraria::item_off::netID;
    size_t npcType = terraria::npc_off::type;
    size_t npcWhoAmI = terraria::npc_off::whoAmI;
    size_t npcFrame = terraria::npc_off::frame;
    size_t heldItem = terraria::player_draw_set_off::heldItem;  // sem cabeçalho (struct por ref)

    FieldInfo* itemAssets = nullptr;     // static Asset<Texture2D>[] TextureAssets.Item
    FieldInfo* npcAssets = nullptr;      // static Asset<Texture2D>[] TextureAssets.Npc
    FieldInfo* npcFrameCount = nullptr;  // static int[] Main.npcFrameCount

    // Criação de textura.
    Il2CppClass* unityTexture2D = nullptr;  // UnityEngine.Texture2D
    Il2CppClass* xnaTexture2D = nullptr;    // Microsoft.Xna.Framework.Graphics.Texture2D
    const MethodInfo* unityCtor = nullptr;            // .ctor(int, int, TextureFormat, bool)
    const MethodInfo* setFilterMode = nullptr;        // Texture.set_filterMode
    const MethodInfo* setWrapMode = nullptr;          // Texture.set_wrapMode
    const MethodInfo* rawImageDataSize = nullptr;     // Texture2D.GetRawImageDataSize()
    const MethodInfo* writableImageData = nullptr;    // Texture2D.GetWritableImageData(int)
    const MethodInfo* apply = nullptr;                // Texture2D.Apply(bool, bool)
    const MethodInfo* xnaCtorFromUnity = nullptr;     // XNA Texture2D.ctor(UnityEngine.Texture2D)
    Il2CppClass* assetClass = nullptr;                // Asset<Texture2D> (de um elemento existente)
    size_t assetName = terraria::asset_texture2d_fields::Name;
    size_t assetState = terraria::asset_texture2d_fields::State;
    size_t assetValue = terraria::asset_texture2d_fields::Value;
};
DrawResolved d;

void* g_origInventorySlot = nullptr;
void* g_origHotbarSlot = nullptr;
void* g_origMainDrawItemSlot = nullptr;
void* g_origDrawItemIconSlot = nullptr;
void* g_origHeldItemSlot = nullptr;
void* g_origDrawNpcSlot = nullptr;

std::atomic<const TLModDrawCallbacks*> g_callbacks{nullptr};

template <typename Fn>
Fn LoadOrig(void* const& slot) {
    return reinterpret_cast<Fn>(__atomic_load_n(&slot, __ATOMIC_ACQUIRE));
}

// Texturas criadas: só a thread principal cria e desenha, mas o tamanho pode
// ser consultado de outra; publicação por contador atômico.
struct TextureRec {
    uint32_t asset;  // GC handle do Asset<Texture2D>
    int32_t width, height;
};
constexpr int32_t kMaxTextures = 4096;
TextureRec g_textures[kMaxTextures];
std::atomic<int32_t> g_textureCount{0};

Il2CppObject* AssetOf(int32_t id) {
    if (id <= 0 || id > g_textureCount.load(std::memory_order_acquire)) return nullptr;
    return api.il2cpp_gchandle_get_target(g_textures[id - 1].asset);
}

template <typename T>
T StaticValue(FieldInfo* f) {
    T v{};
    if (f) api.il2cpp_field_static_get_value(f, &v);
    return v;
}

bool Invoke(const MethodInfo* m, void* obj, void** params, const char* what, Il2CppObject** result = nullptr) {
    if (!m) return false;
    Il2CppException* exc = nullptr;
    Il2CppObject* ret = api.il2cpp_runtime_invoke(m, obj, params, &exc);
    if (exc) {
        bridge::LogException(what, exc);
        return false;
    }
    if (result) *result = ret;
    return true;
}

// .ctor de 1 parâmetro cujo tipo é `paramType` (o XNA Texture2D tem dois:
// UnityEngine.Texture2D e RenderTexture).
const MethodInfo* FindCtor1(Il2CppClass* klass, const char* paramType) {
    void* iter = nullptr;
    while (const MethodInfo* m = api.il2cpp_class_get_methods(klass, &iter)) {
        if (std::strcmp(api.il2cpp_method_get_name(m), ".ctor") != 0 || api.il2cpp_method_get_param_count(m) != 1)
            continue;
        char* name = api.il2cpp_type_get_name(api.il2cpp_method_get_param(m, 0));
        bool match = name && std::strcmp(name, paramType) == 0;
        api.il2cpp_free(name);
        if (match) return m;
    }
    return nullptr;
}

// ---- Troca durante um desenho ------------------------------------------------------
// POD de propósito: os frames dos hooks não têm objetos com destrutor.
struct Swap {
    Il2CppArray* array;
    int32_t index;
    Il2CppObject* previous;
};

Il2CppObject*& Elem(Il2CppArray* arr, int32_t i) { return ArrayData<Il2CppObject*>(arr)[i]; }

bool SwapIn(FieldInfo* assets, int32_t index, Il2CppObject* asset, Swap* out) {
    Il2CppArray* arr = StaticValue<Il2CppArray*>(assets);
    if (!arr || index < 0 || static_cast<uint32_t>(index) >= api.il2cpp_array_length(arr)) return false;
    out->array = arr;
    out->index = index;
    out->previous = Elem(arr, index);
    api.il2cpp_gc_wbarrier_set_field(reinterpret_cast<Il2CppObject*>(arr),
                                     reinterpret_cast<void**>(&Elem(arr, index)), asset);
    return true;
}

void SwapOut(const Swap& s) {
    api.il2cpp_gc_wbarrier_set_field(reinterpret_cast<Il2CppObject*>(s.array),
                                     reinterpret_cast<void**>(&Elem(s.array, s.index)), s.previous);
}

// Antes de desenhar `item`: se for de mod e tiver textura, o Asset da casca
// sai e entra o do mod. Devolve false se nada mudou.
bool BeginItem(Il2CppObject* item, Swap* swap) {
    if (!item || !d.ready) return false;
    int32_t net = FieldAt<int32_t>(item, d.itemNetId);
    if (net < terraria::kItemVanillaCount) return false;  // original: nada a trocar
    const TLModDrawCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
    if (!cb || !cb->item_texture) return false;
    Il2CppObject* asset = AssetOf(cb->item_texture(item, net));
    return asset && SwapIn(d.itemAssets, FieldAt<int32_t>(item, d.itemType), asset, swap);
}

Il2CppObject* ItemAt(Il2CppArray* inv, int32_t slot) {
    if (!inv || slot < 0 || static_cast<uint32_t>(slot) >= api.il2cpp_array_length(inv)) return nullptr;
    return Elem(inv, slot);
}

// NPC: Asset, Main.npcFrameCount[type] (o desenho calcula a origem com ele) e
// NPC.frame (o quadro). Tudo volta no fim do desenho.
struct NpcSwap {
    Swap asset;
    int32_t* frameCount;
    int32_t previousFrameCount;
    TLModXnaRectangle previousFrame;
};

int32_t NpcIndex(Il2CppObject* npc) {
    int32_t w = FieldAt<int32_t>(npc, d.npcWhoAmI);
    if (w >= 0 && w < terraria::kMaxNPCs && TLMod_GetNpcByIndex(w) == npc) return w;
    return TLMod_FindNpcIndex(npc);
}

bool BeginNpc(Il2CppObject* npc, NpcSwap* swap) {
    if (!npc || !d.ready) return false;
    const TLModDrawCallbacks* cb = g_callbacks.load(std::memory_order_acquire);
    if (!cb || !cb->npc_texture) return false;
    TLModNpcDraw draw{};
    if (!cb->npc_texture(npc, NpcIndex(npc), &draw)) return false;
    int32_t id = draw.texture;
    Il2CppObject* asset = AssetOf(id);
    Il2CppArray* counts = StaticValue<Il2CppArray*>(d.npcFrameCount);
    int32_t type = FieldAt<int32_t>(npc, d.npcType);
    if (!asset || !counts || type < 0 || static_cast<uint32_t>(type) >= api.il2cpp_array_length(counts)) return false;
    if (!SwapIn(d.npcAssets, type, asset, &swap->asset)) return false;
    const TextureRec& rec = g_textures[id - 1];
    int32_t frames = draw.frame_count > 0 ? draw.frame_count : 1;
    int32_t frame = draw.frame >= 0 && draw.frame < frames ? draw.frame : 0;
    int32_t frameHeight = rec.height / frames;
    swap->frameCount = &ArrayData<int32_t>(counts)[type];
    swap->previousFrameCount = *swap->frameCount;
    *swap->frameCount = frames;
    TLModXnaRectangle& rect = FieldAt<TLModXnaRectangle>(npc, d.npcFrame);
    swap->previousFrame = rect;
    rect = TLModXnaRectangle{0, frame * frameHeight, rec.width, frameHeight};
    return true;
}

void EndNpc(Il2CppObject* npc, const NpcSwap& swap) {
    FieldAt<TLModXnaRectangle>(npc, d.npcFrame) = swap.previousFrame;
    *swap.frameCount = swap.previousFrameCount;
    SwapOut(swap.asset);
}

}  // namespace

// ===========================================================================
// Resolução e instalação (chamadas por TLMod_Init)
// ===========================================================================
namespace bridge {

bool ResolveDraw() {
    const Il2CppImage* img = Image();
    Il2CppClass* item = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kItemClass);
    Il2CppClass* npc = api.il2cpp_class_from_name(img, terraria::kNamespace, terraria::kNpcClass);
    Il2CppClass* assets = api.il2cpp_class_from_name(img, "Terraria.GameContent", terraria::kTextureAssetsClass);
    Il2CppClass* drawSet = api.il2cpp_class_from_name(img, "Terraria.DataStructures", "PlayerDrawSet");
    if (!item || !npc || !assets) {
        LOGW("desenho: Item=%p NPC=%p TextureAssets=%p; texturas de mod desativadas", static_cast<void*>(item),
             static_cast<void*>(npc), static_cast<void*>(assets));
        return false;
    }
    d.itemType = FieldOffset(item, "type", terraria::item_off::type);
    d.itemNetId = FieldOffset(item, "netID", terraria::item_off::netID);
    d.npcType = FieldOffset(npc, "type", terraria::npc_off::type);
    d.npcWhoAmI = FieldOffset(npc, "whoAmI", terraria::npc_off::whoAmI);
    d.npcFrame = FieldOffset(npc, "frame", terraria::npc_off::frame);
    if (drawSet) {
        // Struct passada por ref: o offset do runtime inclui o cabeçalho de objeto.
        size_t off = FieldOffset(drawSet, "heldItem", terraria::player_draw_set_off::heldItem + kObjectHeader);
        d.heldItem = off - kObjectHeader;
    }
    d.itemAssets = api.il2cpp_class_get_field_from_name(assets, "Item");
    d.npcAssets = api.il2cpp_class_get_field_from_name(assets, "Npc");
    d.npcFrameCount = api.il2cpp_class_get_field_from_name(MainClass(), "npcFrameCount");

    const Il2CppImage* core = FindImage("UnityEngine.CoreModule.dll");
    d.unityTexture2D = core ? api.il2cpp_class_from_name(core, "UnityEngine", "Texture2D") : nullptr;
    Il2CppClass* unityTexture = core ? api.il2cpp_class_from_name(core, "UnityEngine", "Texture") : nullptr;
    d.xnaTexture2D = api.il2cpp_class_from_name(img, "Microsoft.Xna.Framework.Graphics", terraria::kXnaTexture2DClass);
    if (d.unityTexture2D) {
        d.unityCtor = api.il2cpp_class_get_method_from_name(d.unityTexture2D, ".ctor", 4);
        d.rawImageDataSize = api.il2cpp_class_get_method_from_name(d.unityTexture2D, "GetRawImageDataSize", 0);
        d.writableImageData = api.il2cpp_class_get_method_from_name(d.unityTexture2D, "GetWritableImageData", 1);
        d.apply = api.il2cpp_class_get_method_from_name(d.unityTexture2D, "Apply", 2);
    }
    if (unityTexture) {
        d.setFilterMode = api.il2cpp_class_get_method_from_name(unityTexture, "set_filterMode", 1);
        d.setWrapMode = api.il2cpp_class_get_method_from_name(unityTexture, "set_wrapMode", 1);
    }
    if (d.xnaTexture2D) d.xnaCtorFromUnity = FindCtor1(d.xnaTexture2D, "UnityEngine.Texture2D");
    if (!d.unityCtor || !d.rawImageDataSize || !d.writableImageData || !d.apply || !d.xnaCtorFromUnity)
        LOGW("textura: Texture2D(4)=%p GetRawImageDataSize=%p GetWritableImageData=%p Apply(2)=%p "
             "XNA .ctor(UnityEngine.Texture2D)=%p; texturas de mod não serão criadas",
             static_cast<const void*>(d.unityCtor), static_cast<const void*>(d.rawImageDataSize),
             static_cast<const void*>(d.writableImageData), static_cast<const void*>(d.apply),
             static_cast<const void*>(d.xnaCtorFromUnity));
    if (!d.itemAssets || !d.npcAssets || !d.npcFrameCount) {
        LOGW("desenho: TextureAssets.Item=%p TextureAssets.Npc=%p Main.npcFrameCount=%p; desativado",
             static_cast<void*>(d.itemAssets), static_cast<void*>(d.npcAssets), static_cast<void*>(d.npcFrameCount));
        return false;
    }
    d.ready = true;
    return true;
}

void InstallDrawHooks() {
    auto resolve = [](const char* ns, const char* klass, const char* method, int argc) -> const MethodInfo* {
        Il2CppClass* k = api.il2cpp_class_from_name(Image(), ns, klass);
        return k ? api.il2cpp_class_get_method_from_name(k, method, argc) : nullptr;
    };
    InstallHook("GUIInventory.DrawItem", resolve("", "GUIInventory", "DrawItem", terraria::argc::GUIInventory_DrawItem),
                terraria::rva::GUIInventory_DrawItem, reinterpret_cast<void*>(&TLMod_Hooked_GUIInventoryDrawItem),
                &g_origInventorySlot);
    InstallHook("GUIHotbar.DrawHotbarItem",
                resolve("", "GUIHotbar", "DrawHotbarItem", terraria::argc::GUIHotbar_DrawHotbarItem),
                terraria::rva::GUIHotbar_DrawHotbarItem, reinterpret_cast<void*>(&TLMod_Hooked_GUIHotbarDrawHotbarItem),
                &g_origHotbarSlot);
    InstallHook("Main.DrawItem", resolve(terraria::kNamespace, "Main", "DrawItem", terraria::argc::Main_DrawItem),
                terraria::rva::Main_DrawItem, reinterpret_cast<void*>(&TLMod_Hooked_MainDrawItem),
                &g_origMainDrawItemSlot);
    InstallHook("ItemSlot.DrawItemIcon",
                resolve("Terraria.UI", "ItemSlot", "DrawItemIcon", terraria::argc::ItemSlot_DrawItemIcon),
                terraria::rva::ItemSlot_DrawItemIcon, reinterpret_cast<void*>(&TLMod_Hooked_ItemSlotDrawItemIcon),
                &g_origDrawItemIconSlot);
    InstallHook("PlayerDrawLayers.DrawPlayer_27_HeldItem",
                resolve("Terraria.DataStructures", "PlayerDrawLayers", "DrawPlayer_27_HeldItem",
                        terraria::argc::PlayerDrawLayers_DrawPlayer_27_HeldItem),
                terraria::rva::PlayerDrawLayers_DrawPlayer_27_HeldItem,
                reinterpret_cast<void*>(&TLMod_Hooked_DrawPlayerHeldItem), &g_origHeldItemSlot);
    InstallHook("Main.DrawNPCDirect",
                resolve(terraria::kNamespace, "Main", "DrawNPCDirect", terraria::argc::Main_DrawNPCDirect),
                terraria::rva::Main_DrawNPCDirect, reinterpret_cast<void*>(&TLMod_Hooked_MainDrawNPCDirect),
                &g_origDrawNpcSlot);
}

}  // namespace bridge

// ===========================================================================
// API pública
// ===========================================================================
extern "C" {

void TLMod_SetDrawCallbacks(const TLModDrawCallbacks* callbacks) {
    g_callbacks.store(callbacks ? new TLModDrawCallbacks(*callbacks) : nullptr, std::memory_order_release);
}

bool TLMod_TextureSize(int32_t id, int32_t* width, int32_t* height) {
    if (id <= 0 || id > g_textureCount.load(std::memory_order_acquire) || !width || !height) return false;
    *width = g_textures[id - 1].width;
    *height = g_textures[id - 1].height;
    return true;
}

int32_t TLMod_CreateTexture(const uint8_t* rgba, int32_t width, int32_t height, const char* name) {
    if (!d.ready || !d.unityCtor || !d.writableImageData || !d.xnaCtorFromUnity || !rgba || width <= 0 ||
        height <= 0 || width > 4096 || height > 4096) {
        LOGE("textura %s: parâmetros inválidos ou criação indisponível", name ? name : "?");
        return 0;
    }
    if (g_textureCount.load() >= kMaxTextures) {
        LOGE("textura %s: limite de %d texturas", name ? name : "?", kMaxTextures);
        return 0;
    }
    // A classe Asset<Texture2D> (genérica) vem de um elemento que o jogo já criou.
    if (!d.assetClass) {
        for (FieldInfo* f : {d.itemAssets, d.npcAssets}) {
            Il2CppArray* arr = StaticValue<Il2CppArray*>(f);
            for (uint32_t i = 0, n = arr ? api.il2cpp_array_length(arr) : 0; i < n && !d.assetClass; ++i)
                if (Il2CppObject* a = Elem(arr, static_cast<int32_t>(i))) d.assetClass = api.il2cpp_object_get_class(a);
        }
        if (!d.assetClass) return -1;  // TextureAssets ainda não foi preenchido
        d.assetName = bridge::FieldOffset(d.assetClass, "<Name>k__BackingField", terraria::asset_texture2d_fields::Name);
        d.assetState = bridge::FieldOffset(d.assetClass, "<State>k__BackingField", terraria::asset_texture2d_fields::State);
        d.assetValue = bridge::FieldOffset(d.assetClass, "<Value>k__BackingField", terraria::asset_texture2d_fields::Value);
    }

    // UnityEngine.Texture2D(w, h, RGBA32, false), Point, Clamp.
    Il2CppObject* tex = api.il2cpp_object_new(d.unityTexture2D);
    int32_t format = terraria::kTextureFormatRGBA32;
    bool mipChain = false;
    void* ctorParams[] = {&width, &height, &format, &mipChain};
    if (!tex || !Invoke(d.unityCtor, tex, ctorParams, "UnityEngine.Texture2D..ctor")) return 0;
    int32_t point = terraria::kFilterModePoint, clamp = terraria::kWrapModeClamp;
    void* filterParams[] = {&point};
    void* wrapParams[] = {&clamp};
    Invoke(d.setFilterMode, tex, filterParams, "Texture.set_filterMode");
    Invoke(d.setWrapMode, tex, wrapParams, "Texture.set_wrapMode");

    // Pixels direto no buffer da CPU; o Unity guarda a linha de baixo primeiro.
    const size_t row = static_cast<size_t>(width) * 4;
    Il2CppObject* boxed = nullptr;
    if (!Invoke(d.rawImageDataSize, tex, nullptr, "Texture2D.GetRawImageDataSize", &boxed) || !boxed ||
        *static_cast<uint64_t*>(api.il2cpp_object_unbox(boxed)) < row * static_cast<size_t>(height)) {
        LOGE("textura %s: buffer do Unity menor que %dx%d RGBA", name ? name : "?", width, height);
        return 0;
    }
    int32_t image = 0;
    void* dataParams[] = {&image};
    if (!Invoke(d.writableImageData, tex, dataParams, "Texture2D.GetWritableImageData", &boxed) || !boxed) return 0;
    auto* dst = *static_cast<uint8_t**>(api.il2cpp_object_unbox(boxed));
    if (!dst) return 0;
    for (int32_t y = 0; y < height; ++y)
        std::memcpy(dst + static_cast<size_t>(height - 1 - y) * row, rgba + static_cast<size_t>(y) * row, row);
    bool no = false;
    void* applyParams[] = {&no, &no};
    if (!Invoke(d.apply, tex, applyParams, "Texture2D.Apply")) return 0;

    // Invólucro XNA e Asset carregado (Main.LoadItem/LoadNPC não o recarregam).
    Il2CppObject* xna = api.il2cpp_object_new(d.xnaTexture2D);
    void* xnaParams[] = {tex};
    if (!xna || !Invoke(d.xnaCtorFromUnity, xna, xnaParams, "XNA Texture2D..ctor")) return 0;
    Il2CppObject* asset = api.il2cpp_object_new(d.assetClass);
    if (!asset) return 0;
    SetRefField(asset, d.assetName, api.il2cpp_string_new(name ? name : "TLMod"));
    FieldAt<int32_t>(asset, d.assetState) = terraria::kAssetStateLoaded;
    SetRefField(asset, d.assetValue, xna);

    // Vivos pelo processo: o Asset segura o XNA, que segura a do Unity; os três
    // presos também por handle (o Unity não descarrega o que o C# ainda alcança).
    api.il2cpp_gchandle_new(tex, false);
    api.il2cpp_gchandle_new(xna, false);
    int32_t id = g_textureCount.load() + 1;
    g_textures[id - 1] = TextureRec{api.il2cpp_gchandle_new(asset, false), width, height};
    g_textureCount.store(id, std::memory_order_release);
    LOGI("textura %s: %dx%d criada (id %d)", name ? name : "?", width, height, id);
    return id;
}

// ---------------------------------------------------------------------------
// Handlers dos hooks de desenho (thread principal). Só PODs nos frames.
// ---------------------------------------------------------------------------

void TLMod_Hooked_GUIInventoryDrawItem(Il2CppObject* self, Il2CppObject* spriteBatch, Il2CppObject* layout,
                                       Il2CppArray* inv, int32_t slot, TLModXnaVector2 position,
                                       TLModXnaColor lightColor, float scale, const MethodInfo* method) {
    auto orig = LoadOrig<GridDrawItemFn>(g_origInventorySlot);
    if (!orig) return;
    Swap swap{};
    bool swapped = BeginItem(ItemAt(inv, slot), &swap);
    orig(self, spriteBatch, layout, inv, slot, position, lightColor, scale, method);
    if (swapped) SwapOut(swap);
}

void TLMod_Hooked_GUIHotbarDrawHotbarItem(Il2CppObject* self, Il2CppObject* spriteBatch, Il2CppObject* layout,
                                          Il2CppArray* inv, int32_t slot, TLModXnaVector2 position,
                                          TLModXnaColor lightColor, float scale, const MethodInfo* method) {
    auto orig = LoadOrig<GridDrawItemFn>(g_origHotbarSlot);
    if (!orig) return;
    Swap swap{};
    bool swapped = BeginItem(ItemAt(inv, slot), &swap);
    orig(self, spriteBatch, layout, inv, slot, position, lightColor, scale, method);
    if (swapped) SwapOut(swap);
}

void TLMod_Hooked_MainDrawItem(Il2CppObject* self, Il2CppObject* item, int32_t whoami, const MethodInfo* method) {
    auto orig = LoadOrig<MainDrawItemFn>(g_origMainDrawItemSlot);
    if (!orig) return;
    Swap swap{};
    bool swapped = BeginItem(item, &swap);
    orig(self, item, whoami, method);
    if (swapped) SwapOut(swap);
}

float TLMod_Hooked_ItemSlotDrawItemIcon(Il2CppObject* item, int32_t context, Il2CppObject* spriteBatch,
                                        TLModXnaVector2 screenPositionForItemCenter, float scale, float sizeLimit,
                                        TLModXnaColor environmentColor, const MethodInfo* method) {
    auto orig = LoadOrig<DrawItemIconFn>(g_origDrawItemIconSlot);
    if (!orig) return 0.0f;
    Swap swap{};
    bool swapped = BeginItem(item, &swap);
    float result = orig(item, context, spriteBatch, screenPositionForItemCenter, scale, sizeLimit, environmentColor, method);
    if (swapped) SwapOut(swap);
    return result;
}

void TLMod_Hooked_DrawPlayerHeldItem(void* drawinfo, const MethodInfo* method) {
    auto orig = LoadOrig<HeldItemFn>(g_origHeldItemSlot);
    if (!orig) return;
    Swap swap{};
    bool swapped = drawinfo && BeginItem(FieldAt<Il2CppObject*>(drawinfo, d.heldItem), &swap);
    orig(drawinfo, method);
    if (swapped) SwapOut(swap);
}

void TLMod_Hooked_MainDrawNPCDirect(Il2CppObject* self, Il2CppObject* spriteBatch, Il2CppObject* npc, bool behindTiles,
                                    TLModXnaVector2 screenPos, Il2CppObject* lightMap, TLModXnaRectangle* lightRegion,
                                    const MethodInfo* method) {
    auto orig = LoadOrig<DrawNpcDirectFn>(g_origDrawNpcSlot);
    if (!orig) return;
    NpcSwap swap{};
    bool swapped = BeginNpc(npc, &swap);
    orig(self, spriteBatch, npc, behindTiles, screenPos, lightMap, lightRegion, method);
    if (swapped) EndNpc(npc, swap);
}

}  // extern "C"
