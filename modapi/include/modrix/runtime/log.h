#pragma once

#include <string>

#include "modrix/modrix_api.h"

namespace modrix::rt {

// Destino das mensagens do runtime e dos mods. Padrão: stderr.
// O adaptador Android troca por logcat.
using LogSink = void (*)(ModrixLogLevel level, const std::string& source, const std::string& msg);

void SetLogSink(LogSink sink);
void Log(ModrixLogLevel level, const std::string& source, const std::string& msg);

}  // namespace modrix::rt
