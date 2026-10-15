#pragma once

#include <string>

namespace modrix::overlay {

class PanelController;

// Prende o painel à janela da Activity do jogo (Views Android, sem permissão
// de sobreposição). Espera até `timeoutMs` pela Activity do Unity. O
// controlador precisa viver enquanto o jogo roda; depois desta chamada ele só
// é usado na thread de UI do Android.
bool StartOverlay(PanelController* panel, int timeoutMs, std::string* error);

}  // namespace modrix::overlay
