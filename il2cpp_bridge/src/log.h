// Log da ponte: vai para o logcat (stderr no host) e, se o runtime ligou
// TLMod_SetLogCallback, também para o log em arquivo do TLMod. As linhas
// emitidas antes disso ficam guardadas (até kEarlyLines) e são reenviadas.
#pragma once

namespace bridge {
// level: 1 = I, 2 = W, 3 = E (mesmos valores de TLModLogLevel).
void LogLine(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
}  // namespace bridge

#define LOGI(...) ::bridge::LogLine(1, __VA_ARGS__)
#define LOGW(...) ::bridge::LogLine(2, __VA_ARGS__)
#define LOGE(...) ::bridge::LogLine(3, __VA_ARGS__)
