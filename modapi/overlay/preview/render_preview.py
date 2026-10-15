#!/usr/bin/env python3
"""Prévia em PNG do painel do Modrix, desenhada a partir do view-model.

O painel real são Views Android (ModrixOverlay.java), que não rodam no host.
Este script espelha o layout do Java (mesmas medidas em dp, paddings e ordem)
e lê exatamente o JSON que o PanelController entrega ao Java, inclusive o
tema. Serve para ver o resultado antes do aparelho; não é uma captura de tela.

    overlay_snapshot sample_checklist.json out/
    python3 render_preview.py out/snapshot_checklist.json out/snapshot_mods.json preview.png
"""

import json
import sys

from PIL import Image, ImageDraw, ImageFont

DENSITY = 2.0                   # px por dp (a imagem sai em ~xhdpi)
SCREEN_DP = (900, 420)          # celular na horizontal, em dp
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def dp(v):
    return int(round(v * DENSITY))


def rgba(hex_argb):
    h = hex_argb.lstrip("#")
    if len(h) == 6:
        h = "FF" + h
    a, r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4, 6))
    return (r, g, b, a)


_fonts = {}


def font(sp, bold=False):
    key = (sp, bold)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(FONT_BOLD if bold else FONT, dp(sp))
    return _fonts[key]


def line_h(f):
    ascent, descent = f.getmetrics()
    return ascent + descent


class Painter:
    """Desenha numa camada RGBA com as mesmas regras do renderizador Java."""

    def __init__(self, img, theme):
        self.img = img
        self.d = ImageDraw.Draw(img)
        self.t = theme

    def color(self, key):
        return rgba(self.t.get(key, "#FFFF00FF"))

    def rounded(self, box, radius_dp, fill_key, stroke_key=None):
        self.d.rounded_rectangle(box, radius=dp(radius_dp), fill=self.color(fill_key),
                                 outline=self.color(stroke_key) if stroke_key else None,
                                 width=dp(1) if stroke_key else 0)

    def text(self, xy, s, size_key, color_key, bold=False, anchor="la"):
        f = font(self.t[size_key], bold)
        self.d.text(xy, s, font=f, fill=self.color(color_key), anchor=anchor)
        return line_h(f)

    def text_w(self, s, size_key, bold=False):
        return self.d.textlength(s, font=font(self.t[size_key], bold))

    def pill(self, right, cy, label, color_key):
        """Rótulo arredondado (status, Hardmode): padding 8/2 dp, cantos de 10 dp."""
        f = font(self.t["small_sp"], True)
        w = self.d.textlength(label, font=f) + dp(16)
        h = line_h(f) + dp(4)
        box = (right - w, cy - h / 2, right, cy + h / 2)
        self.rounded(box, 10, color_key)
        self.d.text(((box[0] + box[2]) / 2, cy), label, font=f, fill=self.color("panel_bg"), anchor="mm")
        return w


def draw_game_background(img):
    """Cena ilustrativa (céu, chão em blocos), só para dar contexto."""
    w, h = img.size
    d = ImageDraw.Draw(img)
    for y in range(h):
        k = y / h
        d.line([(0, y), (w, y)], fill=(int(70 + 60 * k), int(130 + 70 * k), int(220 - 20 * k), 255))
    ground = int(h * 0.72)
    block = dp(16)
    for x in range(0, w, block):
        for y in range(ground, h, block):
            top = y == ground
            c = (86, 168, 64) if top else ((141, 96, 60) if (x // block + y // block) % 2 else (128, 86, 54))
            d.rectangle([x, y, x + block - 1, y + block - 1], fill=c + (255,))
    for tx in (int(w * 0.62), int(w * 0.83)):  # duas "árvores"
        d.rectangle([tx, ground - dp(80), tx + dp(14), ground], fill=(110, 76, 45, 255))
        d.ellipse([tx - dp(30), ground - dp(130), tx + dp(44), ground - dp(60)], fill=(52, 128, 50, 255))
    f = font(10)
    d.text((w - dp(8), h - dp(6)), "cena do jogo (ilustrativa)", font=f, fill=(255, 255, 255, 170), anchor="rd")


def render_panel(snap, layer, scroll_to_end=False):
    t = snap["theme"]
    p = Painter(layer, t)
    sw, sh = layer.size
    margin = dp(t["margin_dp"])

    # Botão flutuante (Java: leftMargin = margin, topMargin = margin * 6).
    fab_size = dp(t["fab_size_dp"])
    fx, fy = margin, margin * 6
    p.d.ellipse([fx, fy, fx + fab_size, fy + fab_size], fill=p.color("fab_bg"), outline=p.color("accent"),
                width=dp(2))
    p.text((fx + fab_size / 2, fy + fab_size / 2), snap["fab"]["label"], "title_sp", "text", True, "mm")
    if not snap["open"]:
        return

    # Painel ao lado do botão (Java: positionPanel). Altura = conteúdo, até max_h
    # (Java: onMeasure com AT_MOST); o topo usa max_h, como no Java.
    pw = dp(t["panel_width_dp"])
    max_h = min(dp(t["panel_max_height_dp"]), sh - 2 * margin)
    px = min(fx + fab_size + margin, max(0, sw - pw - margin))
    py = max(margin, min(fy, sh - max_h - margin))
    pad = dp(10)
    x0, x1 = px + pad, px + pw - pad
    hh = line_h(font(t["title_sp"], True))
    th = line_h(font(t["text_sp"], True)) + dp(12)
    content_y = py + pad + hh + dp(4) + dp(8) + th + dp(8)

    # Conteúdo rolável desenhado à parte, para medir a altura e recortar.
    def draw_content(top):
        layer_ = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        cp = Painter(layer_, t)
        if snap["tab"] == "mods":
            return layer_, draw_mods(cp, snap["mods"], x0, x1, top)
        return layer_, draw_checklist(cp, snap["checklist"], x0, x1, top)

    content, end_y = draw_content(content_y)
    ph = min(max_h, end_y - py + pad)
    overflow = end_y - (py + ph - pad)
    if scroll_to_end and overflow > 0:
        content, _ = draw_content(content_y - overflow)

    p.rounded((px, py, px + pw, py + ph), t["corner_dp"], "panel_bg", "panel_border")
    y = py + pad

    # Cabeçalho: título + fechar.
    p.text((x0, y), snap["title"], "title_sp", "accent", True)
    p.text((x1 - dp(4), y + dp(2)), "✕", "title_sp", "text_dim", False, "ra")
    y += hh + dp(4)

    # Abas (pesos iguais, padding 10/6, ativa com fundo arredondado).
    y += dp(8)
    tabs = snap["tabs"]
    tw = (x1 - x0) / len(tabs)
    for i, tab in enumerate(tabs):
        bx = x0 + i * tw
        if tab["active"]:
            p.rounded((bx, y, bx + tw, y + th), 8, "tab_active_bg")
        p.text((bx + tw / 2, y + th / 2), tab["label"], "text_sp", "text" if tab["active"] else "text_dim",
               tab["active"], "mm")
    y += th + dp(8)

    assert y == content_y
    clip = (px, y, px + pw, py + ph - pad)
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).rectangle(clip, fill=255)
    layer.paste(content, (0, 0), Image.composite(content, Image.new("RGBA", layer.size), mask).getchannel("A"))


def draw_mods(p, mods, x0, x1, y):
    t = p.t
    if mods.get("notice"):
        h = p.text((x0 + dp(4), y), mods["notice"], "small_sp", "accent")
        y += h + dp(8)
    for r in mods["rows"]:
        lines = [(r["title"], "text_sp", "text", True)]
        state = r["state"]
        lines.append((r["subtitle"], "small_sp", "text_dim" if state == "loaded" else
                      ("unknown" if state == "disabled" else "error"), False))
        if r.get("detail"):
            lines.append((r["detail"], "small_sp", "error", False))
        if r.get("pending"):
            lines.append((r["pending"], "small_sp", "accent", False))
        text_h = sum(line_h(font(t[s], b)) for _, s, _, b in lines)
        row_h = max(text_h, dp(22)) + dp(16)
        p.rounded((x0, y, x1, y + row_h), 8, "row_bg")
        ty = y + dp(8)
        max_w = (x1 - dp(10) - dp(48)) - (x0 + dp(10))
        for s, size, color, bold in lines:
            ty += p.text((x0 + dp(10), ty), ellipsize(p, s, size, bold, max_w), size, color, bold)
        # Interruptor: trilho 40x22 dp, botão de 18 dp.
        tx1 = x1 - dp(10)
        cy = y + row_h / 2
        on = r["enabled"]
        p.rounded((tx1 - dp(40), cy - dp(11), tx1, cy + dp(11)), 11, "switch_on" if on else "switch_off")
        kx = tx1 - dp(20) if on else tx1 - dp(38)
        p.d.ellipse([kx, cy - dp(9), kx + dp(18), cy + dp(9)], fill=p.color("text"))
        y += row_h + dp(6)
    return y - dp(6)


def ellipsize(p, s, size, bold, max_w):
    if p.text_w(s, size, bold) <= max_w:
        return s
    while s and p.text_w(s + "…", size, bold) > max_w:
        s = s[:-1]
    return s + "…"


def draw_checklist(p, c, x0, x1, y):
    t = p.t
    if c["mode"] != "ready":
        # Java: TextView centralizado com padding vertical de 48 dp.
        mh = line_h(font(t["text_sp"]))
        p.text(((x0 + x1) / 2, y + dp(48) + mh / 2), c["message"], "text_sp", "text_dim", False, "mm")
        return y + dp(96) + mh
    # Resumo: três "chips" de peso igual, espaço de 6 dp.
    n = len(c["summary"])
    gap = dp(6)
    cw = (x1 - x0 - gap * (n - 1)) / n
    ch = line_h(font(t["title_sp"], True)) + line_h(font(t["small_sp"])) + dp(12)
    for i, s in enumerate(c["summary"]):
        bx = x0 + i * (cw + gap)
        p.rounded((bx, y, bx + cw, y + ch), 8, "row_bg")
        vh = p.text((bx + cw / 2, y + dp(6)), s["value"], "title_sp", "text", True, "ma")
        p.text((bx + cw / 2, y + dp(6) + vh), s["label"], "small_sp", "text_dim", False, "ma")
    y += ch + dp(8)

    nh = line_h(font(t["text_sp"], True))
    next_w = x1 - x0
    if c.get("hardmode"):
        next_w -= p.pill(x1, y + nh / 2, "Hardmode", "error") + dp(6)
    p.text((x0, y), ellipsize(p, c["next"], "text_sp", True, next_w), "text_sp", "accent", True)
    y += nh + dp(8)

    for r in c["rows"]:
        if r["header"]:
            y += dp(8)
            y += p.text((x0 + dp(4), y), r["name"].upper(), "small_sp", "text_dim", True) + dp(4)
            continue
        nh = line_h(font(t["text_sp"]))
        sh = line_h(font(t["small_sp"]))
        row_h = nh + sh + dp(12)
        p.rounded((x0, y, x1, y + row_h), 8, "row_bg")
        pill_w = p.pill(x1 - dp(8), y + row_h / 2, r["status_label"], r["status"])
        max_w = (x1 - dp(8) - pill_w) - (x0 + dp(10)) - dp(6)
        p.text((x0 + dp(10), y + dp(6)), ellipsize(p, r["name"], "text_sp", False, max_w), "text_sp", "text")
        p.text((x0 + dp(10), y + dp(6) + nh), r["category"], "small_sp", "text_dim")
        y += row_h + dp(4)
    return y - dp(4)


def render_screen(snap, caption, scroll_to_end=False):
    w, h = dp(SCREEN_DP[0]), dp(SCREEN_DP[1])
    screen = Image.new("RGBA", (w, h))
    draw_game_background(screen)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    render_panel(snap, layer, scroll_to_end)
    screen = Image.alpha_composite(screen, layer)
    d = ImageDraw.Draw(screen)
    d.text((w - dp(10), dp(8)), caption, font=font(12, True), fill=(255, 255, 255, 230), anchor="ra",
           stroke_width=dp(1), stroke_fill=(0, 0, 0, 160))
    return screen


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    *snaps, out = sys.argv[1:]
    screens = []
    for path in snaps:
        snap = json.load(open(path, encoding="utf-8"))
        if snap["tab"] == "checklist":
            screens.append(render_screen(snap, "Aba Boss Checklist"))
            screens.append(render_screen(snap, "Aba Boss Checklist (rolada até o fim)", True))
        else:
            screens.append(render_screen(snap, "Aba Mods"))
    gap = dp(10)
    total = Image.new("RGBA", (screens[0].width, sum(s.height for s in screens) + gap * (len(screens) - 1)),
                      (24, 24, 28, 255))
    y = 0
    for s in screens:
        total.paste(s, (0, y))
        y += s.height + gap
    total.convert("RGB").save(out, optimize=True)
    print(f"{out}: {total.width}x{total.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
