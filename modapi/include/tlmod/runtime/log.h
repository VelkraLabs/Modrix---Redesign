#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tlmod/tlmod_api.h"

namespace tlmod::rt {

// Destino das mensagens do runtime e dos mods. Padrão: stderr.
// O adaptador Android troca por logcat.
using LogSink = void (*)(TLModLogLevel level, const std::string& source, const std::string& msg);

void SetLogSink(LogSink sink);

// Registra a linha (memória e arquivo, se houver) e manda para o sink.
void Log(TLModLogLevel level, const std::string& source, const std::string& msg);

// Só registra (memória e arquivo), sem passar pelo sink: para linhas que já
// foram para o logcat por outro caminho (as da ponte). timeMs = 0 usa a hora atual.
void RecordLog(TLModLogLevel level, const std::string& source, const std::string& msg, int64_t timeMs = 0);

// Uma linha registrada.
struct LogEntry {
    int64_t timeMs = 0;  // Unix, em milissegundos
    TLModLogLevel level = TLMOD_LOG_INFO;
    std::string source;
    std::string message;
    // "2026-10-16 12:34:56.789 E [fonte] mensagem" (hora local)
    std::string Format() const;
};

// Últimas linhas em memória (no máximo kLogMemoryLines), da mais antiga para a mais nova.
constexpr size_t kLogMemoryLines = 500;
std::vector<LogEntry> RecentLog(size_t max = kLogMemoryLines);

// Passa a gravar também em `file` (append; cria a pasta). As linhas que já
// estavam na memória e ainda não foram gravadas entram primeiro. Quando o
// arquivo passa de kLogFileMaxBytes, vira <file>.1 (substituindo o anterior)
// e um novo começa. Vazio desliga o arquivo.
constexpr uintmax_t kLogFileMaxBytes = 1024 * 1024;
bool SetLogFile(const std::filesystem::path& file, std::string* error = nullptr);
std::filesystem::path LogFilePath();

// Só para testes: esvazia a memória, fecha o arquivo e volta ao sink padrão.
void ResetLogForTest();

}  // namespace tlmod::rt
