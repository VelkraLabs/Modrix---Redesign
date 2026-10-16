// Cópia privada das bibliotecas dos mods antes do dlopen.
//
// No Android, a pasta de mods fica no armazenamento externo
// (/storage/emulated/0/Android/data/<pacote>/files/tlmod/mods), que é montado
// noexec e fica fora dos caminhos permitidos do namespace do linker: o dlopen
// direto de lá falha. A biblioteca de cada mod é copiada para uma pasta privada
// do app (code_cache/tlmod/<mod_id>/) e carregada da cópia.
//
// A cópia:
//   - é atômica: grava <nome>.tmp-<pid>-<n> na mesma pasta e só então faz rename
//     sobre o destino; um processo morto no meio deixa no máximo o temporário,
//     que é apagado na próxima vez;
//   - é conferida: o SHA-256 do temporário precisa bater com o da origem;
//   - fica somente leitura (0444), como o Android 14 exige para código
//     carregado dinamicamente;
//   - é reaproveitada quando o SHA-256 da cópia já bate com o da origem.
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace tlmod::rt {

class LibraryCache {
public:
    explicit LibraryCache(std::filesystem::path root) : root_(std::move(root)) {}

    struct Result {
        std::filesystem::path path;  // a cópia, de onde fazer o dlopen
        bool copied = false;         // false = reaproveitada
        std::string sha256;
    };

    // Prepara a cópia de `source` em <root>/<modId>/<nome do arquivo>.
    // nullopt + error se não der (a origem não é lida, a pasta não é criada,
    // o hash não confere...). Nesse caso nenhum arquivo parcial fica no destino.
    std::optional<Result> Prepare(const std::string& modId, const std::filesystem::path& source,
                                  std::string* error) const;

    const std::filesystem::path& Root() const { return root_; }

    // Só para testes: chamado com o temporário já gravado, antes da conferência
    // do hash (permite simular uma cópia corrompida).
    std::function<void(const std::filesystem::path& tmp)> afterWriteForTest;

private:
    std::filesystem::path root_;
};

}  // namespace tlmod::rt
