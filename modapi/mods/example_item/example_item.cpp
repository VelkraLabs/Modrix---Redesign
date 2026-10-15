// Mod de exemplo: registra um item fictício. No jogo ele roda numa casca (a
// Lente, padrão): as tabelas do jogo só veem a Lente, mas o nome, o tooltip, o
// valor, a raridade, a pilha máxima e a imagem (textures/fictional_ruby.png,
// 32x32) são os daqui.
//
// Para testar no aparelho: crie um arquivo vazio chamado `give_on_start` na
// pasta deste mod. Ao entrar num mundo, o jogador recebe 3 Rubis Fictícios
// (uma vez por abertura do jogo).
#include <tlmod/tlmod.hpp>

#include <cstdio>
#include <string>

static bool FileExists(const std::string& path) {
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

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
    ruby.texture = "textures/fictional_ruby.png";  // relativo à pasta do mod

    TLModId id = host.RegisterItem(ruby);
    if (id == TLMOD_INVALID_ID) return false;  // motivo já está no log do runtime

    host.Info("Rubi Fictício registrado com ID " + std::to_string(id));
    if (FileExists(std::string(host.ModDir()) + "/give_on_start")) {
        // A entrega espera o jogador estar num mundo (ver give_item em tlmod_api.h).
        if (host.GiveItem(id, 3)) host.Info("give_on_start: 3 Rubis Fictícios ao entrar num mundo");
        else host.Warn("give_on_start: este runtime não tem give_item");
    }
    return true;
}

TLMOD_MOD(OnLoad)
