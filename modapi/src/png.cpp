#include "tlmod/runtime/png.h"

#include <zlib.h>

#include <cstring>
#include <fstream>
#include <iterator>

namespace tlmod::rt {
namespace {

uint32_t Be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}

int Paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// Amostra `index` (0..) de uma linha de `bits` bits por amostra, em 0..255.
uint8_t Sample(const uint8_t* row, size_t index, int bits) {
    switch (bits) {
        case 8: return row[index];
        case 16: return row[index * 2];  // o byte mais significativo basta para RGBA8
        default: {
            size_t bit = index * static_cast<size_t>(bits);
            int shift = 8 - bits - static_cast<int>(bit % 8);
            int v = (row[bit / 8] >> shift) & ((1 << bits) - 1);
            return static_cast<uint8_t>(v * 255 / ((1 << bits) - 1));  // escala para 0..255
        }
    }
}

uint8_t RawIndex(const uint8_t* row, size_t index, int bits) {  // paleta: sem escala
    if (bits == 8) return row[index];
    size_t bit = index * static_cast<size_t>(bits);
    int shift = 8 - bits - static_cast<int>(bit % 8);
    return static_cast<uint8_t>((row[bit / 8] >> shift) & ((1 << bits) - 1));
}

}  // namespace

std::optional<Image> DecodePng(const std::vector<uint8_t>& data, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<Image> {
        if (error) *error = msg;
        return std::nullopt;
    };
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (data.size() < 8 || std::memcmp(data.data(), kSignature, 8) != 0) return fail("não é um PNG (assinatura)");

    uint32_t width = 0, height = 0;
    int bitDepth = 0, colorType = -1;
    bool haveHeader = false, ended = false;
    std::vector<uint8_t> idat, palette, trns;
    size_t pos = 8;
    while (pos < data.size()) {
        if (data.size() - pos < 12) return fail("PNG truncado");
        uint32_t len = Be32(&data[pos]);
        if (len > data.size() - pos - 12) return fail("PNG truncado");
        const uint8_t* type = &data[pos + 4];
        const uint8_t* body = &data[pos + 8];
        uint32_t crc = Be32(body + len);
        if (crc32(crc32(0L, Z_NULL, 0), type, len + 4) != crc)
            return fail("CRC errado no chunk " + std::string(reinterpret_cast<const char*>(type), 4));
        std::string name(reinterpret_cast<const char*>(type), 4);
        if (name == "IHDR") {
            if (len != 13) return fail("IHDR inválido");
            width = Be32(body);
            height = Be32(body + 4);
            bitDepth = body[8];
            colorType = body[9];
            if (body[10] != 0 || body[11] != 0) return fail("compressão ou filtro desconhecido");
            if (body[12] != 0) return fail("PNG entrelaçado (Adam7) não é aceito: salve sem entrelaçamento");
            haveHeader = true;
        } else if (name == "PLTE") {
            palette.assign(body, body + len);
        } else if (name == "tRNS") {
            trns.assign(body, body + len);
        } else if (name == "IDAT") {
            idat.insert(idat.end(), body, body + len);
        } else if (name == "IEND") {
            ended = true;
            break;
        } else if (!(type[0] & 0x20)) {
            return fail("chunk crítico desconhecido " + name);
        }
        pos += 12 + len;
    }
    if (!haveHeader) return fail("PNG sem IHDR");
    if (!ended) return fail("PNG sem IEND (truncado)");
    if (width == 0 || height == 0 || width > static_cast<uint32_t>(kMaxImageSide) ||
        height > static_cast<uint32_t>(kMaxImageSide))
        return fail("tamanho " + std::to_string(width) + "x" + std::to_string(height) + " fora de 1.." +
                    std::to_string(kMaxImageSide));

    int channels;
    switch (colorType) {
        case 0: channels = 1; break;  // cinza
        case 2: channels = 3; break;  // RGB
        case 3: channels = 1; break;  // paleta
        case 4: channels = 2; break;  // cinza + alfa
        case 6: channels = 4; break;  // RGBA
        default: return fail("tipo de cor " + std::to_string(colorType) + " inválido");
    }
    bool depthOk = bitDepth == 8 || (bitDepth == 16 && colorType != 3) ||
                   ((bitDepth == 1 || bitDepth == 2 || bitDepth == 4) && (colorType == 0 || colorType == 3));
    if (!depthOk) return fail("profundidade " + std::to_string(bitDepth) + " inválida para o tipo de cor");
    if (colorType == 3 && (palette.empty() || palette.size() % 3 != 0)) return fail("PNG de paleta sem PLTE");

    const size_t bitsPerPixel = static_cast<size_t>(channels) * static_cast<size_t>(bitDepth);
    const size_t stride = (static_cast<size_t>(width) * bitsPerPixel + 7) / 8;
    const size_t bpp = bitsPerPixel >= 8 ? bitsPerPixel / 8 : 1;  // distância do filtro
    std::vector<uint8_t> raw(static_cast<size_t>(height) * (stride + 1));

    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return fail("zlib: inflateInit");
    zs.next_in = idat.data();
    zs.avail_in = static_cast<uInt>(idat.size());
    zs.next_out = raw.data();
    zs.avail_out = static_cast<uInt>(raw.size());
    int rc = inflate(&zs, Z_FINISH);
    size_t produced = raw.size() - zs.avail_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || produced != raw.size()) return fail("dados da imagem corrompidos ou incompletos");

    // Desfaz os filtros linha a linha (no lugar).
    std::vector<uint8_t> zero(stride, 0);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* line = &raw[y * (stride + 1)];
        uint8_t filter = line[0];
        uint8_t* cur = line + 1;
        const uint8_t* prev = y ? &raw[(y - 1) * (stride + 1) + 1] : zero.data();
        for (size_t i = 0; i < stride; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            switch (filter) {
                case 0: break;
                case 1: cur[i] = static_cast<uint8_t>(cur[i] + a); break;
                case 2: cur[i] = static_cast<uint8_t>(cur[i] + b); break;
                case 3: cur[i] = static_cast<uint8_t>(cur[i] + ((a + b) >> 1)); break;
                case 4: cur[i] = static_cast<uint8_t>(cur[i] + Paeth(a, b, c)); break;
                default: return fail("filtro de linha " + std::to_string(filter) + " inválido");
            }
        }
    }

    Image img;
    img.width = static_cast<int32_t>(width);
    img.height = static_cast<int32_t>(height);
    img.rgba.resize(static_cast<size_t>(width) * height * 4);
    // tRNS em cinza/RGB: uma cor exata vira transparente (comparada em 16 bits).
    auto key16 = [&](size_t i) -> int { return trns.size() >= i * 2 + 2 ? (trns[i * 2] << 8) | trns[i * 2 + 1] : -1; };
    auto full16 = [&](const uint8_t* row, size_t index) -> int {
        if (bitDepth == 16) return (row[index * 2] << 8) | row[index * 2 + 1];
        if (bitDepth == 8) return row[index];
        return RawIndex(row, index, bitDepth);
    };
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = &raw[y * (stride + 1) + 1];
        uint8_t* out = &img.rgba[static_cast<size_t>(y) * width * 4];
        for (uint32_t x = 0; x < width; ++x, out += 4) {
            switch (colorType) {
                case 0: {
                    out[0] = out[1] = out[2] = Sample(row, x, bitDepth);
                    out[3] = (key16(0) >= 0 && full16(row, x) == key16(0)) ? 0 : 255;
                    break;
                }
                case 2: {
                    for (int c = 0; c < 3; ++c) out[c] = Sample(row, x * 3 + static_cast<size_t>(c), bitDepth);
                    bool key = key16(2) >= 0 && full16(row, x * 3) == key16(0) && full16(row, x * 3 + 1) == key16(1) &&
                               full16(row, x * 3 + 2) == key16(2);
                    out[3] = key ? 0 : 255;
                    break;
                }
                case 3: {
                    uint8_t i = RawIndex(row, x, bitDepth);
                    if (static_cast<size_t>(i) * 3 + 2 >= palette.size()) return fail("índice fora da paleta");
                    std::memcpy(out, &palette[static_cast<size_t>(i) * 3], 3);
                    out[3] = i < trns.size() ? trns[i] : 255;
                    break;
                }
                case 4: {
                    out[0] = out[1] = out[2] = Sample(row, x * 2, bitDepth);
                    out[3] = Sample(row, x * 2 + 1, bitDepth);
                    break;
                }
                default: {  // 6
                    for (int c = 0; c < 4; ++c) out[c] = Sample(row, x * 4 + static_cast<size_t>(c), bitDepth);
                }
            }
        }
    }
    return img;
}

std::optional<Image> LoadPng(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = file.string() + ": não foi possível abrir";
        return std::nullopt;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    std::optional<Image> img = DecodePng(data, &err);
    if (!img && error) *error = file.string() + ": " + err;
    return img;
}

}  // namespace tlmod::rt
