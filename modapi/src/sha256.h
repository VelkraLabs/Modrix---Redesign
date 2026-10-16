// SHA-256 (FIPS 180-4) sem dependências: o Android não traz OpenSSL público.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace tlmod::rt {

class Sha256 {
public:
    Sha256();
    void Update(const void* data, size_t len);
    std::string HexDigest();  // finaliza; depois disso o objeto não aceita mais Update

private:
    void Block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};

std::string Sha256Hex(const void* data, size_t len);
// Hash do arquivo inteiro; nullopt se não abrir ou falhar na leitura.
std::optional<std::string> Sha256File(const std::filesystem::path& path, std::string* error = nullptr);

}  // namespace tlmod::rt
