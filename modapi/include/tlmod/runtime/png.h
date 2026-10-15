#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tlmod::rt {

// Imagem RGBA8, linha 0 = topo (como no PNG).
struct Image {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4
};

// Maior lado aceito (texturas de item/NPC; o Unity aceita mais, mas um PNG
// desse tamanho num mod é quase sempre engano).
constexpr int32_t kMaxImageSide = 4096;

// Decodificador PNG mínimo (zlib para o IDAT). Aceita todos os tipos de cor
// (cinza, RGB, paleta com tRNS, cinza+alfa, RGBA) em 1/2/4/8/16 bits, com
// todos os filtros. Não aceita entrelaçamento (Adam7): devolve erro dizendo isso.
// Confere assinatura, CRC de cada chunk e tamanho dos dados.
std::optional<Image> DecodePng(const std::vector<uint8_t>& data, std::string* error);
std::optional<Image> LoadPng(const std::filesystem::path& file, std::string* error);

}  // namespace tlmod::rt
