#!/usr/bin/env python3
"""Gera os PNGs dos mods de exemplo (pixel art determinística, só stdlib).

    python3 tools/make_example_textures.py

Saídas (versionadas; rode de novo só se mudar o desenho):
  modapi/mods/example_item/textures/fictional_ruby.png   32x32, o Rubi Fictício
  modapi/mods/example_boss/textures/ember_titan.png      64x256, 4 quadros de
                                                         64x64 empilhados na vertical
"""
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def write_png(path, width, height, pixels):
    """pixels: lista de linhas (topo primeiro) de tuplas RGBA."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # filtro None
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)
    print(f"{path.relative_to(ROOT)}: {width}x{height}, {len(png)} bytes")


CLEAR = (0, 0, 0, 0)


def ruby():
    """Gema lapidada vista de frente: mesa no topo, pavilhão em ponta."""
    w = h = 32
    outline = (74, 6, 24, 255)
    light, mid, dark, deep = (255, 120, 150, 255), (224, 32, 72, 255), (168, 14, 50, 255), (118, 8, 36, 255)
    shine = (255, 230, 236, 255)
    px = [[CLEAR] * w for _ in range(h)]
    cx = 15.5
    top, girdle, tip = 6, 13, 27  # linhas: topo da coroa, cintura, ponta
    for y in range(top, tip + 1):
        if y <= girdle:  # coroa: trapézio que alarga até a cintura
            half = 6 + (y - top) * (13 - 6) / (girdle - top)
        else:  # pavilhão: triângulo até a ponta
            half = 13 * (tip - y) / (tip - girdle)
        for x in range(w):
            d = x - cx
            if abs(d) > half + 0.01:
                continue
            if y <= girdle:
                c = light if abs(d) < half * 0.45 else (mid if d < 0 else dark)
            else:
                c = mid if d < -half * 0.3 else (dark if d < half * 0.3 else deep)
            px[y][x] = c
    # contorno: pixel opaco com vizinho transparente
    out = [row[:] for row in px]
    for y in range(h):
        for x in range(w):
            if px[y][x][3] == 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < w and 0 <= ny < h) or px[ny][nx][3] == 0:
                    out[y][x] = outline
                    break
    for x, y in ((12, 8), (13, 8), (12, 9), (11, 10)):  # brilho
        out[y][x] = shine
    for x in range(w):  # linha da cintura
        if out[girdle][x] not in (CLEAR, outline):
            out[girdle][x] = deep
    return w, h, out


def ember_titan(frames=4, size=64):
    """Orbe de brasa com um olho; as chamas em volta giram quadro a quadro."""
    rows = []
    c = (size - 1) / 2
    for f in range(frames):
        phase = f * (2 * math.pi / frames) / 6  # 6 línguas de fogo: 1/4 de passo por quadro
        for y in range(size):
            row = []
            for x in range(size):
                dx, dy = x - c, y - c
                r = math.hypot(dx, dy)
                ang = math.atan2(dy, dx)
                flame = 23 + 6 * max(0.0, math.cos(6 * (ang - phase)))
                if r <= 19:  # corpo
                    t = r / 19
                    col = (int(255 - 90 * t), int(170 - 140 * t), int(40 - 30 * t), 255)
                    if r >= 17.5:
                        col = (70, 14, 4, 255)
                    eye = math.hypot(dx, dy + 1)
                    if eye <= 7:
                        col = (250, 245, 220, 255)
                    pupil_x = 2 * math.cos(f * math.pi / 2)  # o olho varre de lado a lado
                    if math.hypot(dx - pupil_x, dy + 1) <= 3.5:
                        col = (30, 4, 4, 255)
                elif r <= flame:  # chamas
                    t = (r - 19) / max(1.0, flame - 19)
                    col = (255, int(200 - 150 * t), int(60 - 50 * t), int(255 - 120 * t))
                else:
                    col = CLEAR
                row.append(col)
            rows.append(row)
    return size, size * frames, rows


def main():
    w, h, px = ruby()
    write_png(ROOT / "modapi/mods/example_item/textures/fictional_ruby.png", w, h, px)
    w, h, px = ember_titan()
    write_png(ROOT / "modapi/mods/example_boss/textures/ember_titan.png", w, h, px)


if __name__ == "__main__":
    main()
