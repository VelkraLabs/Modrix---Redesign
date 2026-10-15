// Mod de exemplo: registra um item fictício e nada mais.
#include <tlmod/tlmod.hpp>

#include <string>

static bool OnLoad(tlmod::Host& host) {
    tlmod::ItemDef ruby;
    ruby.name = "fictional_ruby";  // precisa estar em content.items do manifest.json
    ruby.displayName = "Rubi Fictício";
    ruby.tooltip = "Não existe no Terraria. Existe no TLMod.";
    ruby.width = 22;
    ruby.height = 22;
    ruby.maxStack = 999;
    ruby.value = 5000;  // 50 moedas de prata
    ruby.rarity = 2;

    TLModId id = host.RegisterItem(ruby);
    if (id == TLMOD_INVALID_ID) return false;  // motivo já está no log do runtime

    host.Info("Rubi Fictício registrado com ID " + std::to_string(id));
    return true;
}

TLMOD_MOD(OnLoad)
