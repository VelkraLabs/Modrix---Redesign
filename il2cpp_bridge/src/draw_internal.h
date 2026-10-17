// Interno à ponte: a troca de textura de item durante um desenho, usada pelos
// hooks de draw_hooks.cpp e de screen_hooks.cpp.
#pragma once

#include <cstdint>

#include "bridge.h"
#include "il2cpp_api.h"

namespace drawx {

// POD de propósito: os frames dos hooks não têm objetos com destrutor.
struct Swap {
    Il2CppArray* array;
    int32_t index;
    Il2CppObject* previous;
};
struct ItemSwap {
    Swap tex;   // TextureAssets.Item[type]
    Swap anim;  // Main.itemAnimations[type]
    bool texture, animation;
};

// Item de mod com textura: troca TextureAssets.Item[casca] (e tira a animação
// da casca) até EndItem. false = nada trocado (item original, sem textura...).
bool BeginItem(Il2CppObject* item, ItemSwap* swap);
void EndItem(const ItemSwap& swap);

// Há callback de textura e pelo menos uma textura criada: sem isso nenhum
// BeginItem troca nada, e os hooks não precisam nem achar o item.
bool ItemTexturesActive();

int32_t ItemType(Il2CppObject* item);
bool IsModItem(Il2CppObject* item);  // netID na faixa de mods
int32_t NpcIndex(Il2CppObject* npc);  // posição em Main.npc, -1 se não for um deles
const TLModDrawCallbacks* Callbacks();
// Ícone de um item (Oficina de Mods): o XNA Texture2D da textura do mod (pelo
// callback item_texture) ou de TextureAssets.Item[id], e o tamanho. Nulo se o
// asset ainda não carregou (para um item original, pede a carga ao jogo uma
// vez). Só na thread do jogo.
Il2CppObject* ItemIconTexture(int32_t itemId, int32_t* width, int32_t* height);

}  // namespace drawx
