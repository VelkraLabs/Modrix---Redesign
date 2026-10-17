#!/usr/bin/env python3
"""Prévia em PNG do painel do TLMod, desenhada a partir do view-model.

O painel real são Views Android (TLModOverlay.java), que não rodam no host.
Este script espelha o layout do Java (mesmas medidas em dp, paddings e ordem)
e lê exatamente o JSON que o PanelController entrega ao Java, inclusive o
tema. Serve para ver o resultado antes do aparelho; não é uma captura de tela.

    overlay_snapshot sample_checklist.json out/ ../../mods/devtools/sample_panel.json
    python3 render_preview.py out/snapshot_checklist.json out/snapshot_mods.json out/snapshot_log.json \
        out/snapshot_tools.json preview.png
    python3 render_preview.py out/snapshot_workshop.json workshop.png     # Oficina de Mods
"""

import json
import sys

from PIL import Image, ImageDraw, ImageFont

DENSITY = 2.0                   # px por dp (a imagem sai em ~xhdpi)
SCREEN_DP = (900, 420)          # celular na horizontal, em dp
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"


def dp(v):
    return int(round(v * DENSITY))


def rgba(hex_argb):
    h = hex_argb.lstrip("#")
    if len(h) == 6:
        h = "FF" + h
    a, r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4, 6))
    return (r, g, b, a)


_fonts = {}


def font(sp, bold=False, mono=False):
    key = (sp, bold, mono)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(FONT_MONO if mono else (FONT_BOLD if bold else FONT), dp(sp))
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

    def text_w(self, s, size_key, bold=False, mono=False):
        return self.d.textlength(s, font=font(self.t[size_key], bold, mono))

    def wrap(self, s, size_key, max_w, bold=False, mono=False):
        """Quebra como o TextView: por palavra; palavra maior que a linha é cortada por caractere."""
        out, cur = [], ""
        for word in s.split(" "):
            cand = word if not cur else cur + " " + word
            if self.text_w(cand, size_key, bold, mono) <= max_w:
                cur = cand
                continue
            if cur:
                out.append(cur)
            cur = ""
            for ch in word:
                if cur and self.text_w(cur + ch, size_key, bold, mono) > max_w:
                    out.append(cur)
                    cur = ""
                cur += ch
        out.append(cur)
        return out

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
        if snap["tab"] == "log":
            return layer_, draw_log(cp, snap["log"], x0, x1, top)
        if snap["tab"].startswith("mod:"):
            return layer_, draw_modtab(cp, snap["modtab"], x0, x1, top)
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

    # Abas (Java: largura do texto + parte igual da sobra, padding 10/6, ativa com fundo arredondado).
    y += dp(8)
    tabs = snap["tabs"]
    natural = [p.text_w(tab["label"], "text_sp", tab["active"]) + dp(20) for tab in tabs]
    extra = max(0, (x1 - x0) - sum(natural)) / len(tabs)
    bx = x0
    for tab, nw in zip(tabs, natural):
        tw = nw + extra
        if tab["active"]:
            p.rounded((bx, y, bx + tw, y + th), 8, "tab_active_bg")
        p.text((bx + tw / 2, y + th / 2), tab["label"], "text_sp", "text" if tab["active"] else "text_dim",
               tab["active"], "mm")
        bx += tw
    y += th + dp(8)

    assert y == content_y
    clip = (px, y, px + pw, py + ph - pad)
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).rectangle(clip, fill=255)
    layer.paste(content, (0, 0), Image.composite(content, Image.new("RGBA", layer.size), mask).getchannel("A"))


def draw_mods(p, mods, x0, x1, y):
    t = p.t
    if mods.get("workshop_button"):  # Java: botão de largura toda, padding 10/8, fundo accent
        bh = line_h(font(t["text_sp"], True)) + dp(16)
        p.rounded((x0, y, x1, y + bh), 8, "accent")
        p.text(((x0 + x1) / 2, y + bh / 2), mods["workshop_button"], "text_sp", "panel_bg", True, "mm")
        y += bh + dp(8)
    if mods.get("notice"):
        h = p.text((x0 + dp(4), y), mods["notice"], "small_sp", "accent")
        y += h + dp(8)
    for r in mods["rows"]:
        max_w = (x1 - dp(10) - dp(48)) - (x0 + dp(10))
        lines = [(ellipsize(p, r["title"], "text_sp", True, max_w), "text_sp", "text", True)]
        state = r["state"]
        lines.append((ellipsize(p, r["subtitle"], "small_sp", False, max_w), "small_sp", "text_dim" if state == "loaded"
                      else ("unknown" if state == "disabled" else "error"), False))
        if r.get("detail"):  # Java: erro inteiro, quebra linha (padding 2 dp em cima e embaixo)
            wrapped = p.wrap(r["detail"], "small_sp", max_w)
            lines.append(("", None, None, None))
            lines += [(s, "small_sp", "error", False) for s in wrapped]
            lines.append(("", None, None, None))
        if r.get("pending"):
            lines.append((ellipsize(p, r["pending"], "small_sp", False, max_w), "small_sp", "accent", False))
        text_h = sum(line_h(font(t[s], b)) if s else dp(2) for _, s, _, b in lines)
        row_h = max(text_h, dp(22)) + dp(16)
        p.rounded((x0, y, x1, y + row_h), 8, "row_bg")
        ty = y + dp(8)
        for s, size, color, bold in lines:
            ty += p.text((x0 + dp(10), ty), s, size, color, bold) if size else dp(2)
        # Interruptor: trilho 40x22 dp, botão de 18 dp.
        tx1 = x1 - dp(10)
        cy = y + row_h / 2
        on = r["enabled"]
        p.rounded((tx1 - dp(40), cy - dp(11), tx1, cy + dp(11)), 11, "switch_on" if on else "switch_off")
        kx = tx1 - dp(20) if on else tx1 - dp(38)
        p.d.ellipse([kx, cy - dp(9), kx + dp(18), cy + dp(9)], fill=p.color("text"))
        y += row_h + dp(6)
    return y - dp(6)


def draw_log(p, log, x0, x1, y):
    t = p.t
    # Caminho do arquivo (small, text_dim, padding 4/0/4/6).
    for s in p.wrap(log.get("file") or "Sem arquivo de log", "small_sp", x1 - x0 - dp(8)):
        y += p.text((x0 + dp(4), y), s, "small_sp", "text_dim")
    y += dp(6)
    if not log["lines"]:
        mh = line_h(font(t["text_sp"]))
        p.text(((x0 + x1) / 2, y + dp(48) + mh / 2), log["empty"], "text_sp", "text_dim", False, "mm")
        return y + dp(96) + mh
    # Um bloco só, monoespaçado, cor por nível (Java: SpannableStringBuilder num TextView).
    f = font(t["small_sp"], False, True)
    lh = line_h(f)
    rows = []
    for line in log["lines"]:
        key = {"E": "error", "W": "pending"}.get(line["level"], "text")
        rows += [(s, key) for s in p.wrap(line["text"], "small_sp", x1 - x0 - dp(16), False, True)]
    h = len(rows) * lh + dp(12)
    p.rounded((x0, y, x1, y + h), 8, "row_bg")
    ty = y + dp(6)
    for s, key in rows:
        p.d.text((x0 + dp(8), ty), s, font=f, fill=p.color(key))
        ty += lh
    return y + h


def chip_w(p, label):
    return p.text_w(label, "small_sp", True) + dp(20)


def chip(p, x, y, label, active):
    """Java chip(): padding 10/5 dp, cantos 12, destaque quando ligado."""
    f = font(p.t["small_sp"], True)
    w, h = chip_w(p, label), line_h(f) + dp(10)
    p.rounded((x, y, x + w, y + h), 12, "switch_on" if active else "tab_active_bg")
    p.d.text((x + w / 2, y + h / 2), label, font=f, fill=p.color("panel_bg" if active else "text"), anchor="mm")
    return w, h


def draw_modtab(p, tab, x0, x1, y):
    """Java renderModTab(): aviso, seções (título, busca, nota, linhas de botões)."""
    t = p.t
    if tab.get("notice"):
        y += p.text((x0 + dp(4), y), tab["notice"], "small_sp", "accent", True) + dp(8)
    for i, sec in enumerate(tab["content"].get("sections", [])):
        y += dp(0 if i == 0 else 10)
        y += p.text((x0 + dp(4), y), sec.get("title", "").upper(), "small_sp", "text_dim", True) + dp(4)
        if sec.get("search"):
            s = sec["search"]
            fh = line_h(font(t["text_sp"])) + dp(12)
            bw = p.text_w("Buscar", "small_sp", True) + dp(24)
            p.rounded((x0, y, x1 - bw - dp(6), y + fh), 8, "row_bg", "panel_border")
            q = s.get("query") or s.get("placeholder", "")
            p.text((x0 + dp(10), y + dp(6)), q, "text_sp", "text" if s.get("query") else "text_dim")
            p.rounded((x1 - bw, y + dp(2), x1, y + fh - dp(2)), 8, "tab_active_bg")
            p.text((x1 - bw / 2, y + fh / 2), "Buscar", "small_sp", "text", True, "mm")
            y += fh + dp(4)
        if sec.get("note"):
            for s in p.wrap(sec["note"], "small_sp", x1 - x0 - dp(8)):
                y += p.text((x0 + dp(4), y), s, "small_sp", "text_dim")
            y += dp(4)
        for row in sec.get("rows", []):
            buttons = row.get("buttons", [])
            texts = [(row[k], s, c) for k, s, c in (("text", "text_sp", "text"), ("detail", "small_sp", "text_dim"))
                     if row.get(k)]
            ch = line_h(font(t["small_sp"], True)) + dp(10)
            if texts and len(buttons) <= 3:  # texto à esquerda, botões na mesma linha
                bw = sum(chip_w(p, b["label"]) + dp(6) for b in buttons)
                th = sum(line_h(font(t[s])) for _, s, _ in texts)
                h = max(th, ch) + dp(12)
                p.rounded((x0, y, x1, y + h), 8, "row_bg")
                ty = y + (h - th) / 2
                for s, size, color in texts:
                    ty += p.text((x0 + dp(10), ty), ellipsize(p, s, size, False, x1 - x0 - bw - dp(24)), size, color)
                bx = x1 - dp(8) - bw
                for b in buttons:
                    bx += dp(6)
                    w, _ = chip(p, bx, y + (h - ch) / 2, b["label"], b.get("active"))
                    bx += w
                y += h + dp(4)
                continue
            # muitos botões: quebram em linhas (Java: medida do texto + padding)
            avail = x1 - x0 - dp(38)
            lines, cur, used = [], [], 0
            for b in buttons:
                w = chip_w(p, b["label"]) + dp(6)
                if cur and used + w > avail:
                    lines.append(cur)
                    cur, used = [], 0
                cur.append(b)
                used += w
            if cur:
                lines.append(cur)
            th = sum(line_h(font(t[s])) for _, s, _ in texts)
            h = th + len(lines) * (ch + dp(4)) + dp(12)
            p.rounded((x0, y, x1, y + h), 8, "row_bg")
            ty = y + dp(6)
            for s, size, color in texts:
                ty += p.text((x0 + dp(10), ty), s, size, color)
            for ln in lines:
                ty += dp(4)
                bx = x0 + dp(10)
                for b in ln:
                    bx += dp(6)
                    w, _ = chip(p, bx, ty, b["label"], b.get("active"))
                    bx += w
                ty += ch
            y += h + dp(4)
    return y


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


def draw_toggle(p, x1, cy, on):
    """Interruptor do Java: trilho 40x22 dp, botão de 18 dp."""
    p.rounded((x1 - dp(40), cy - dp(11), x1, cy + dp(11)), 11, "switch_on" if on else "switch_off")
    kx = x1 - dp(20) if on else x1 - dp(38)
    p.d.ellipse([kx, cy - dp(9), kx + dp(18), cy + dp(9)], fill=p.color("text"))


def draw_workshop_row(p, r, x0, x1, y):
    """Java: workshopRow. Cartão com nome, versão/autores, estado, interruptor e detalhes."""
    t = p.t
    pad_x, pad_y = dp(12), dp(10)
    ix0, ix1 = x0 + pad_x, x1 - pad_x
    pill_w = p.text_w(r["state_label"], "small_sp", True) + dp(16)
    names_w = ix1 - dp(48) - pill_w - ix0
    by = r["version"]
    if r.get("authors"):
        by = r["authors"] if not by else by + " · " + r["authors"]
    top = [(ellipsize(p, r["name"], "text_sp", True, names_w), "text_sp", "text", True)]
    if by:
        top.append((ellipsize(p, by, "small_sp", False, names_w), "small_sp", "text_dim", False))
    top_h = max(sum(line_h(font(t[s], b)) for _, s, _, b in top), dp(22))
    blocks = []  # (linhas, tamanho, cor, negrito, espaço acima, fundo)
    for key, color, gap in (("description", "text", 6), ("content", "text_dim", 4), ("pending", "accent", 4)):
        if r.get(key):
            blocks.append((p.wrap(r[key], "small_sp", ix1 - ix0), "small_sp", color, False, dp(gap), False))
    if r.get("error_toggle"):
        blocks.append(([r["error_toggle"]], "small_sp", "error", True, dp(6), False))
    if r.get("error"):
        blocks.append((p.wrap(r["error"], "small_sp", ix1 - ix0 - dp(16)), "small_sp", "error", False, dp(2), True))
    body_h = 0
    for lines, s, _, b, gap, boxed in blocks:
        body_h += gap + len(lines) * line_h(font(t[s], b)) + (dp(12) if boxed else 0)
    h = pad_y * 2 + top_h + body_h
    p.rounded((x0, y, x1, y + h), 10, "row_bg")
    ty = y + pad_y + (top_h - sum(line_h(font(t[s], b)) for _, s, _, b in top)) / 2
    for s, size, color, bold in top:
        ty += p.text((ix0, ty), s, size, color, bold)
    cy = y + pad_y + top_h / 2
    draw_toggle(p, ix1, cy, r["enabled"])
    state = r["state"]
    p.pill(ix1 - dp(48), cy, r["state_label"], "defeated" if state == "loaded" else
           ("unknown" if state == "disabled" else "error"))
    ty = y + pad_y + top_h
    for lines, s, color, bold, gap, boxed in blocks:
        ty += gap
        lh = line_h(font(t[s], bold))
        if boxed:
            p.rounded((ix0, ty, ix1, ty + len(lines) * lh + dp(12)), 6, "panel_bg")
            ty += dp(6)
        for ln in lines:
            p.text((ix0 + (dp(8) if boxed else 0), ty), ln, s, color, bold)
            if color == "error" and bold:  # "Ver o erro": sublinhado
                yy = ty + lh - dp(2)
                p.d.line([(ix0, yy), (ix0 + p.text_w(ln, s, True), yy)], fill=p.color("error"), width=dp(1))
            ty += lh
        if boxed:
            ty += dp(6)
    return y + h


def render_workshop(snap, layer, scroll_to_end=False):
    """Java: buildWorkshop/renderWorkshop. Fundo escurecido e cartão centralizado."""
    t = snap["theme"]
    ws = snap["workshop"]
    p = Painter(layer, t)
    sw, sh = layer.size
    p.d.rectangle([0, 0, sw, sh], fill=(0, 0, 0, 0xB3))
    margin = dp(t["margin_dp"])
    cw = min(sw - 2 * margin, dp(720))
    cx0, cy0, cx1, cy1 = (sw - cw) / 2, margin, (sw + cw) / 2, sh - margin
    p.rounded((cx0, cy0, cx1, cy1), t["corner_dp"], "panel_bg", "panel_border")
    pad = dp(14)
    x0, x1 = cx0 + pad, cx1 - pad
    y = cy0 + pad
    title_f = font(t["title_sp"] + 4, True)
    p.d.text((x0, y), ws["title"], font=title_f, fill=p.color("accent"))
    sy = y + line_h(title_f)
    p.text((x0, sy), ws["summary"], "small_sp", "text_dim")
    header_h = line_h(title_f) + line_h(font(t["small_sp"]))
    # Fechar: padding 14/8, fundo tab_active_bg.
    cf = font(t["text_sp"], True)
    bw, bh = p.d.textlength(ws["close_label"], font=cf) + dp(28), line_h(cf) + dp(16)
    by = y + (header_h - bh) / 2
    p.rounded((x1 - bw, by, x1, by + bh), 8, "tab_active_bg")
    p.text((x1 - bw / 2, by + bh / 2), ws["close_label"], "text_sp", "text", True, "mm")
    y += max(header_h, bh)
    if ws.get("notice"):
        y += dp(8)
        y += p.text((x0, y), ws["notice"], "small_sp", "accent", True)
    foot_h = dp(8) + line_h(font(t["small_sp"]))
    p.text((x0, cy1 - pad - line_h(font(t["small_sp"]))), ws["footer"], "small_sp", "text_dim")
    top, bottom = y + dp(8), cy1 - pad - foot_h

    def draw_content(yy):
        content = Image.new("RGBA", layer.size, (0, 0, 0, 0))
        cp = Painter(content, t)
        if ws.get("empty"):
            yy += dp(12) + cp.text((x0, yy + dp(12)), ws["empty"], "text_sp", "text_dim") + dp(12)
        for i, sec in enumerate(ws["sections"]):
            yy += dp(2 if i == 0 else 12)
            yy += cp.text((x0 + dp(2), yy), sec["title"], "text_sp", "accent", True) + dp(6)
            for r in sec["rows"]:
                yy = draw_workshop_row(cp, r, x0, x1, yy) + dp(8)
        return content, yy

    content, end = draw_content(top)
    if scroll_to_end and end > bottom:
        content, _ = draw_content(top - (end - bottom))
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).rectangle((cx0, top, cx1, bottom), fill=255)
    layer.paste(content, (0, 0), Image.composite(content, Image.new("RGBA", layer.size), mask).getchannel("A"))


def render_screen(snap, caption, scroll_to_end=False):
    w, h = dp(SCREEN_DP[0]), dp(SCREEN_DP[1])
    screen = Image.new("RGBA", (w, h))
    draw_game_background(screen)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if snap.get("workshop", {}).get("open"):
        render_workshop(snap, layer, scroll_to_end)
    else:
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
        if snap.get("workshop", {}).get("open"):
            screens.append(render_screen(snap, "Oficina de Mods (botão Mods do menu)"))
            screens.append(render_screen(snap, "Oficina de Mods (rolada até o fim, erro aberto)", True))
        elif snap["tab"] == "checklist":
            screens.append(render_screen(snap, "Aba Boss Checklist"))
            screens.append(render_screen(snap, "Aba Boss Checklist (rolada até o fim)", True))
        elif snap["tab"] == "log":
            screens.append(render_screen(snap, "Aba Log (acompanha o fim)", True))
        elif snap["tab"].startswith("mod:"):
            screens.append(render_screen(snap, "Aba Ferramentas (tlmod.devtools)"))
            screens.append(render_screen(snap, "Aba Ferramentas (rolada até o fim)", True))
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
