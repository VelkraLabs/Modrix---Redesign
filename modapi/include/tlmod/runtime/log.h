#pragma once

#include <string>

#include "tlmod/tlmod_api.h"

namespace tlmod::rt {

// Destino das mensagens do runtime e dos mods. Padrão: stderr.
// O adaptador Android troca por logcat.
using LogSink = void (*)(TLModLogLevel level, const std::string& source, const std::string& msg);

void SetLogSink(LogSink sink);
void Log(TLModLogLevel level, const std::string& source, const std::string& msg);

}  // namespace tlmod::rt
