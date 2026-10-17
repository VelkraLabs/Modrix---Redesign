// Aba "Mods" das Configurações do jogo (sem desenho).
//
// A ponte (il2cpp_bridge/src/settings_tab.cpp) põe uma aba a mais na tela de
// Configurações, com o ícone da Chave Inglesa, e desenha as linhas abaixo
// com as entradas do próprio jogo: um botão (como "Editar controles") e barras
// deslizantes (como "Sensibilidade da mira"). Esta classe diz o que vai em cada
// linha e aplica os toques na configuração.
//
//   0  Posição dos botões de mods   [Editar]   fecha as Configurações e abre o
//                                              modo de edição do overlay
//   1  Tamanho                      50%..150%   (o jogo desenha o valor logo depois do
//   2  Opacidade                    30%..100%    título: título comprido fica embaixo dele)
//   "Padrão" da tela: tamanho, opacidade e posições voltam ao padrão.
//
// Thread-safe (a ponte chama da thread do jogo): só usa a ConfigStore e o
// pedido atômico do PanelController.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tlmod::overlay {

class ConfigStore;
class PanelController;

struct SettingsRow {
    enum class Kind { Button, Slider };
    Kind kind = Kind::Button;
    std::string label;   // botão: o texto da linha; barra: o título
    std::string value;   // botão: o texto do botão; barra: o valor ("100%")
    int32_t steps = 0;   // barra: o valor vai de 0 a steps
    int32_t step = 0;    // barra: o passo atual
};

class ModSettingsTab {
public:
    static constexpr const char* kTitle = "Mods";
    static constexpr int32_t kIconItem = 509;  // Chave Inglesa (prateada: aparece no azul da aba)
    static constexpr int32_t kStepPct = 10;     // cada passo das barras: 10%
    enum Row : int32_t { kRowPosition = 0, kRowScale = 1, kRowOpacity = 2, kRowCount = 3 };

    // `panel` pode ser nulo (sem overlay: o botão de editar só avisa no log).
    ModSettingsTab(ConfigStore& config, PanelController* panel) : config_(config), panel_(panel) {}

    std::vector<SettingsRow> Rows() const;
    // Toque no botão da linha. true = fechar as Configurações.
    bool Click(int32_t row);
    // Barra movida para `step` (0..steps).
    void SetStep(int32_t row, int32_t step);
    // "Padrão" com a aba aberta.
    void Reset();

private:
    ConfigStore& config_;
    PanelController* panel_;
};

}  // namespace tlmod::overlay
