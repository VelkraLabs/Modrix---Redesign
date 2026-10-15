// Mod de exemplo: registra um item fictício e nada mais.
#include <modrix/modrix.hpp>

#include <string>

static bool OnLoad(modrix::Host& host) {
    modrix::ItemDef ruby;
    ruby.name = "fictional_ruby";  // precisa estar em content.items do manifest.json
    ruby.displayName = "Rubi Fictício";
    ruby.tooltip = "Não existe no Terraria. Existe no Modrix.";
    ruby.width = 22;
    ruby.height = 22;
    ruby.maxStack = 999;
    ruby.value = 5000;  // 50 moedas de prata
    ruby.rarity = 2;

    ModrixId id = host.RegisterItem(ruby);
    if (id == MODRIX_INVALID_ID) return false;  // motivo já está no log do runtime

    host.Info("Rubi Fictício registrado com ID " + std::to_string(id));
    return true;
}

MODRIX_MOD(OnLoad)
