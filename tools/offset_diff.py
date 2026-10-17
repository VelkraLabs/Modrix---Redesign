#!/usr/bin/env python3
"""Compara os offsets/RVAs registrados no TLMod com o dump de um APK novo.

A cada atualização do Terraria Mobile:

    python3 tools/offset_diff.py --dump novo/dump.cs [--script novo/script.json]

e o relatório diz o que reconferir:

  * campos com offset diferente (ou tipo diferente) em terraria_offsets.h
  * métodos com RVA diferente, ou cujo número de parâmetros mudou
  * símbolos que sumiram (classe, campo, método)
  * constantes do jogo que mudaram (Main.maxNPCs, NPCID.Count, ItemID.Count...)
  * nomes resolvidos em runtime que não existem mais (flags do Boss Checklist)

Fontes do dump:
  dump.cs      campos (offsets, tipos, consts) e métodos (RVA, parâmetros)
  script.json  só métodos (endereço + assinatura C); sem offsets de campo

Código de saída: 0 = nada mudou, 1 = há diferenças, 2 = erro de uso/leitura.
Só usa a biblioteca padrão (Python 3.8+).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

REPO = Path(__file__).resolve().parent.parent
DEFAULT_HEADER = REPO / "il2cpp_bridge" / "include" / "terraria_offsets.h"
DEFAULT_PROGRESS_SRC = REPO / "modapi" / "adapters" / "bridge" / "bridge_adapter.cpp"
DEFAULT_REGISTRY_SRC = REPO / "modapi" / "src" / "content_registry.cpp"

# ---------------------------------------------------------------------------
# Regras: como cada constante de terraria_offsets.h vira um símbolo do dump.
# Acrescente aqui ao criar um namespace novo no header.
# ---------------------------------------------------------------------------

# namespace do header -> (namespace C#, classe, "instance" | "static")
FIELD_GROUPS: Dict[str, Tuple[str, str, str]] = {
    "player_off": ("Terraria", "Player", "instance"),
    "npc_off": ("Terraria", "NPC", "instance"),
    "main_static_off": ("Terraria", "Main", "static"),
    "local_user_off": ("", "LocalUserGameState", "instance"),
    "item_off": ("Terraria", "Item", "instance"),
    "chest_off": ("Terraria", "Chest", "instance"),
    "loadout_off": ("Terraria", "EquipmentLoadout", "instance"),
    "file_data_off": ("Terraria.IO", "FileData", "instance"),
    "player_file_data_off": ("Terraria.IO", "PlayerFileData", "instance"),
    "world_gen_static_off": ("Terraria", "WorldGen", "static"),
    "item_tooltip_off": ("Terraria.UI", "ItemTooltip", "instance"),
    "item_tooltip_static_off": ("Terraria.UI", "ItemTooltip", "static"),
    "texture_assets_static_off": ("Terraria.GameContent", "TextureAssets", "static"),
    "xna_texture2d_off": ("Microsoft.Xna.Framework.Graphics", "Texture2D", "instance"),
    "player_draw_set_off": ("Terraria.DataStructures", "PlayerDrawSet", "instance"),
    "item_id_sets_static_off": ("Terraria.ID", "ItemID.Sets", "static"),
    "gui_instance_static_off": ("", "GUIInstance", "static"),
    "gui_instance_off": ("", "GUIInstance", "instance"),
    "gui_settings_off": ("", "GUISettings", "instance"),
    "main_menu_layout_static_off": ("", "MainMenu_Layout", "static"),
    "main_menu_layout_off": ("", "MainMenu_Layout", "instance"),
    "button_layout_off": ("", "Button_Layout", "instance"),
    "world_select_layout_static_off": ("", "WorldSelectMenu_Layout", "static"),
    "world_select_layout_off": ("", "WorldSelectMenu_Layout", "instance"),
    "panel_layout_off": ("", "Panel_Layout", "instance"),
    "item_grid_layout_off": ("", "ItemGrid_Layout", "instance"),
    "string_layout_off": ("", "String_Layout", "instance"),
    "string_button_layout_off": ("", "StringButton_Layout", "instance"),
    "gui_crafting_off": ("", "GUICrafting", "instance"),
    "gui_craft_guide_popup_off": ("", "GUICraftGuidePopup", "instance"),
    "recipe_off": ("Terraria", "Recipe", "instance"),
    "item_snippet_off": ("Terraria.GameContent.UI.Chat", "ItemTagHandler.ItemSnippet", "instance"),
    "item_id_static_off": ("Terraria.ID", "ItemID", "static"),
    "npc_id_static_off": ("Terraria.ID", "NPCID", "static"),
}

# Prefixo de rva::/argc:: (antes do primeiro "_") -> (namespace C#, classe).
# O resto do nome é o método: Main_get_gameMenu -> Main.get_gameMenu.
METHOD_CLASSES: Dict[str, Tuple[str, str]] = {
    "Player": ("Terraria", "Player"),
    "NPC": ("Terraria", "NPC"),
    "Main": ("Terraria", "Main"),
    "WorldFile": ("Terraria.IO", "WorldFile"),
    "CreativeUI": ("Terraria.GameContent.Creative", "CreativeUI"),
    "CreativeItemSacrificesCatalog": ("Terraria.GameContent.Creative", "CreativeItemSacrificesCatalog"),
    "Item": ("Terraria", "Item"),
    "Lang": ("Terraria", "Lang"),
    "IdDictionary": ("ReLogic.Reflection", "IdDictionary"),
    "GUIInventory": ("", "GUIInventory"),
    "GUIHotbar": ("", "GUIHotbar"),
    "ItemSlot": ("Terraria.UI", "ItemSlot"),
    "PlayerDrawLayers": ("Terraria.DataStructures", "PlayerDrawLayers"),
    "GUITransactionButton": ("", "GUITransactionButton"),
    "GUIReforgePopup": ("", "GUIReforgePopup"),
    "GUIResearch": ("", "GUIResearch"),
    "GUICraftGuidePopup": ("", "GUICraftGuidePopup"),
    "GUICrafting": ("", "GUICrafting"),
    "GUIVirtualInputController": ("", "GUIVirtualInputController"),
    "GUIQuickActionButton": ("", "GUIQuickActionButton"),
    "GUIMainMenu": ("", "GUIMainMenu"),
    "ControlAnchor": ("", "ControlAnchor"),
    "GUIPanel": ("", "GUIPanel"),
    "GUIString": ("", "GUIString"),
    "GUIStringButton": ("", "GUIStringButton"),
    "SpriteBatch": ("Microsoft.Xna.Framework.Graphics", "SpriteBatch"),
    "StringLayout": ("", "String_Layout"),
    "KeyboardInput": ("", "KeyboardInput"),
    "LayoutCalculator": ("", "LayoutCalculator"),
    "Mouse": ("Microsoft.Xna.Framework.Input", "Mouse"),
    "GUIInputRegionManager": ("", "GUIInputRegionManager"),
    "SoundEngine": ("Terraria.Audio", "SoundEngine"),
    "ItemSnippet": ("Terraria.GameContent.UI.Chat", "ItemTagHandler.ItemSnippet"),
}

# Constantes do header que espelham uma const do jogo.
CONST_CHECKS: Dict[str, Tuple[str, str, str]] = {
    "kMaxNPCs": ("Terraria", "Main", "maxNPCs"),
    "kMaxWorldItems": ("Terraria", "Main", "maxItems"),
    "kNpcVanillaCount": ("Terraria.ID", "NPCID", "Count"),
    "kMaxChests": ("Terraria", "Main", "maxChests"),
    "kItemVanillaCount": ("Terraria.ID", "ItemID", "Count"),
    "kDefaultItemShell": ("Terraria.ID", "ItemID", "Lens"),
}

# Constantes do header que nomeiam classes (namespace onde devem existir).
CLASS_NAME_CONSTANTS: Dict[str, str] = {
    "kPlayerClass": "Terraria",
    "kMainClass": "Terraria",
    "kNpcClass": "Terraria",
    "kLocalUserGameStateClass": "",
    "kItemClass": "Terraria",
    "kChestClass": "Terraria",
    "kEquipmentLoadoutClass": "Terraria",
    "kWorldGenClass": "Terraria",
    "kFileDataClass": "Terraria.IO",
    "kPlayerFileDataClass": "Terraria.IO",
    "kWorldFileClass": "Terraria.IO",
    "kItemTooltipClass": "Terraria.UI",
    "kLocalizedTextClass": "Terraria.Localization",
    "kTextureAssetsClass": "Terraria.GameContent",
    "kXnaTexture2DClass": "Microsoft.Xna.Framework.Graphics",
    "kItemIdClass": "Terraria.ID",
    "kGuiInstanceClass": "",
}

# content_registry.cpp DefaultRanges: tipo -> const com a contagem original.
REGISTRY_KINDS: Dict[str, Tuple[str, str, str]] = {
    "item": ("Terraria.ID", "ItemID", "Count"),
    "npc": ("Terraria.ID", "NPCID", "Count"),
    "projectile": ("Terraria.ID", "ProjectileID", "Count"),
    "tile": ("Terraria.ID", "TileID", "Count"),
    "wall": ("Terraria.ID", "WallID", "Count"),
    "buff": ("Terraria.ID", "BuffID", "Count"),
}

# ---------------------------------------------------------------------------
# Modelo do dump
# ---------------------------------------------------------------------------


@dataclass
class Field:
    name: str
    type: str
    is_static: bool
    offset: Optional[int] = None       # None para const
    const_value: Optional[str] = None  # texto do valor, para const
    thread_static: bool = False


@dataclass
class Method:
    name: str
    rva: Optional[int]   # None = abstrato/sem corpo (RVA: -1)
    params: int
    signature: str


@dataclass
class TypeDef:
    namespace: str
    name: str
    bases: List[str]
    kind: str = "class"  # class | struct | enum | interface
    fields: Dict[str, Field] = field(default_factory=dict)
    methods: Dict[str, List[Method]] = field(default_factory=dict)


class Dump:
    def __init__(self) -> None:
        self.types: Dict[Tuple[str, str], TypeDef] = {}
        self.by_name: Dict[str, List[TypeDef]] = {}
        self.images: List[str] = []
        self.has_fields = False   # dump.cs traz campos; script.json não
        self.sources: List[str] = []

    def add_type(self, t: TypeDef) -> TypeDef:
        key = (t.namespace, t.name)
        if key in self.types:  # script.json depois do dump.cs: mescla métodos
            return self.types[key]
        self.types[key] = t
        self.by_name.setdefault(t.name, []).append(t)
        return t

    def get(self, namespace: str, name: str) -> Optional[TypeDef]:
        t = self.types.get((namespace, name))
        if t is None and "." in name:
            # Classe aninhada ("ItemID.Sets"): o dump.cs a lista com namespace vazio.
            t = self.types.get(("", name))
        return t

    def base_of(self, t: TypeDef) -> Optional[TypeDef]:
        """Classe-base: a primeira da lista de bases que não seja interface."""
        for b in t.bases:
            b = re.sub(r"<.*>$", "", b.strip())  # Singleton<T> -> Singleton
            cands = [c for c in self.by_name.get(b, []) if c.kind != "interface"]
            if not cands:
                continue
            # Prefere o mesmo namespace (o dump não qualifica o nome da base).
            same_ns = [c for c in cands if c.namespace == t.namespace]
            return (same_ns or cands)[0]
        return None

    def find_field(self, t: TypeDef, name: str) -> Tuple[Optional[Field], Optional[TypeDef]]:
        """Procura na classe e nas bases (como il2cpp_class_get_field_from_name)."""
        seen = set()
        cur: Optional[TypeDef] = t
        while cur is not None and id(cur) not in seen:
            seen.add(id(cur))
            if name in cur.fields:
                return cur.fields[name], cur
            cur = self.base_of(cur)
        return None, None


# ---------------------------------------------------------------------------
# Leitura do dump.cs
# ---------------------------------------------------------------------------

TYPE_DECL = re.compile(
    r"^(?:\[.*\]\s*)?(?:(?:public|internal|private|protected|static|sealed|abstract|readonly|ref|unsafe)\s+)*"
    r"(?P<kind>class|struct|enum|interface)\s+(?P<name>[^\s:]+)(?:\s*:\s*(?P<bases>[^/]+?))?\s*//\s*TypeDefIndex"
)
FIELD_LINE = re.compile(
    r"^\t(?P<mods>(?:(?:public|private|protected|internal|static|readonly|const|volatile|new|fixed)\s+)+)"
    r"(?P<type>.+?)\s+(?P<name>[\w<>`$.]+)(?:\s*=\s*(?P<const>.+?))?;\s*(?://\s*(?P<offset>0x[0-9A-Fa-f]+))?\s*$"
)
RVA_LINE = re.compile(r"^\t// RVA: (?P<rva>-1|0x[0-9A-Fa-f]+)")
METHOD_LINE = re.compile(r"^\t(?P<head>[^()]*?)\b(?P<name>[\w.<>`|$]+)\((?P<params>.*)\)\s*\{\s*\}\s*$")


def count_params(params: str) -> int:
    """Conta parâmetros no nível 0 (ignora vírgulas em <...>, (...), [...] e strings)."""
    params = params.strip()
    if not params:
        return 0
    depth, n, quote = 0, 1, None
    for ch in params:
        if quote:
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
        elif ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        elif ch == "," and depth == 0:
            n += 1
    return n


def parse_dump_cs(path: Path, dump: Dump) -> None:
    dump.has_fields = True
    dump.sources.append(str(path))
    namespace = ""
    current: Optional[TypeDef] = None
    section = None
    pending_rva: Optional[str] = None
    pending_thread_static = False
    with open(path, encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if line.startswith("// Image "):
                m = re.match(r"// Image \d+: (\S+)", line)
                if m:
                    dump.images.append(m.group(1))
                continue
            if line.startswith("// Namespace:"):
                namespace = line[len("// Namespace:"):].strip()
                continue
            if current is None:
                m = TYPE_DECL.match(line)
                if m:
                    bases = [b.strip() for b in (m.group("bases") or "").split(",") if b.strip()]
                    current = dump.add_type(TypeDef(namespace, m.group("name"), bases, m.group("kind")))
                    section = None
                continue
            if line.startswith("}"):
                current, section, pending_rva = None, None, None
                continue
            stripped = line.strip()
            if stripped.startswith("// Fields"):
                section = "fields"
                continue
            if stripped.startswith("// Properties"):
                section = "properties"
                continue
            if stripped.startswith("// Methods"):
                section = "methods"
                continue
            if section == "fields":
                if stripped == "[ThreadStatic]":
                    pending_thread_static = True
                    continue
                if stripped.startswith("["):
                    continue
                m = FIELD_LINE.match(line)
                if m:
                    mods = m.group("mods").split()
                    is_const = "const" in mods
                    off = m.group("offset")
                    current.fields[m.group("name")] = Field(
                        name=m.group("name"),
                        type=m.group("type").strip(),
                        is_static=("static" in mods) or is_const,
                        offset=int(off, 16) if off and not is_const else None,
                        const_value=m.group("const").strip() if is_const and m.group("const") else None,
                        thread_static=pending_thread_static,
                    )
                pending_thread_static = False
            elif section == "methods":
                m = RVA_LINE.match(line)
                if m:
                    pending_rva = m.group("rva")
                    continue
                if stripped.startswith("["):
                    continue
                m = METHOD_LINE.match(line)
                if m and pending_rva is not None:
                    rva = None if pending_rva == "-1" else int(pending_rva, 16)
                    meth = Method(m.group("name"), rva, count_params(m.group("params")), stripped)
                    current.methods.setdefault(meth.name, []).append(meth)
                    pending_rva = None


# ---------------------------------------------------------------------------
# Leitura do script.json (Il2CppDumper)
# ---------------------------------------------------------------------------

def c_signature_params(sig: str) -> int:
    """Parâmetros C# a partir da assinatura C: tira __this e o MethodInfo* final."""
    inner = sig[sig.find("(") + 1: sig.rfind(")")]
    parts = [p.strip() for p in inner.split(",") if p.strip()]
    if parts and parts[-1].endswith("method") and "MethodInfo" in parts[-1]:
        parts = parts[:-1]
    if parts and parts[0].endswith("__this"):
        parts = parts[1:]
    return len(parts)


def parse_script_json(path: Path, dump: Dump) -> None:
    dump.sources.append(str(path))
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    from_script: Dict[Tuple[str, str], Dict[str, List[Method]]] = {}
    for m in data.get("ScriptMethod", []):
        full = m.get("Name", "")
        if "$$" not in full:
            continue
        cls, meth = full.split("$$", 1)
        ns, _, name = cls.rpartition(".")
        # Tipos aninhados viram "Ns.Outer.Inner": o dump.cs os chama "Outer.Inner".
        t = dump.get(ns, name)
        if t is None and "." in ns:
            outer_ns, _, outer = ns.rpartition(".")
            if dump.get(outer_ns, f"{outer}.{name}"):
                ns, name = outer_ns, f"{outer}.{name}"
        sig = m.get("Signature", "")
        from_script.setdefault((ns, name), {}).setdefault(meth, []).append(
            Method(meth, m.get("Address"), c_signature_params(sig), sig))
    for (ns, name), methods in from_script.items():
        t = dump.get(ns, name)
        if t is None:
            t = dump.add_type(TypeDef(ns, name, []))
        if not t.methods:  # sem dump.cs: os métodos vêm só daqui
            t.methods = methods


# ---------------------------------------------------------------------------
# Leitura do que o TLMod registra
# ---------------------------------------------------------------------------

@dataclass
class HeaderConst:
    namespace: str     # "player_off", "rva", "" (topo de terraria::)
    name: str
    value: str
    comment: str
    line: int

    @property
    def int_value(self) -> Optional[int]:
        v = self.value.strip()
        try:
            return int(v, 0)
        except ValueError:
            return None

    @property
    def qualified(self) -> str:
        return f"{self.namespace}::{self.name}" if self.namespace else self.name


CONSTEXPR = re.compile(
    r"^\s*constexpr\s+[\w:\s\*]+?\s+(?P<name>\w+)\s*=\s*(?P<value>[^;]+);\s*(?://\s*(?P<comment>.*))?$"
)


def parse_header(path: Path) -> List[HeaderConst]:
    consts: List[HeaderConst] = []
    stack: List[str] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        m = re.match(r"^\s*namespace\s+(\w+)\s*\{", line)
        if m:
            stack.append(m.group(1))
            continue
        if re.match(r"^\s*\}\s*(//.*)?$", line) and stack:
            stack.pop()
            continue
        m = CONSTEXPR.match(line)
        if m:
            inner = [s for s in stack if s != "terraria"]
            consts.append(HeaderConst(".".join(inner), m.group("name"), m.group("value").strip(),
                                      (m.group("comment") or "").strip(), lineno))
    return consts


def parse_progress_fields(path: Path) -> List[Tuple[str, str, str, int]]:
    """bridge_adapter.cpp: {TLMOD_FLAG, "Classe", "campo"} -> (flag, classe, campo, linha)."""
    out = []
    if not path.exists():
        return out
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        m = re.search(r'\{\s*(TLMOD_\w+)\s*,\s*"(\w+)"\s*,\s*"(\w+)"\s*\}', line)
        if m:
            out.append((m.group(1), m.group(2), m.group(3), lineno))
    return out


def parse_registry_ranges(path: Path) -> List[Tuple[str, int, int, int, int]]:
    """content_registry.cpp DefaultRanges: {"item", 5456, 8192, 32767} -> (tipo, orig, 1º mod, máx, linha)."""
    out = []
    if not path.exists():
        return out
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        m = re.search(r'\{\s*"(\w+)"\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', line)
        if m and m.group(1) in REGISTRY_KINDS:
            out.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), lineno))
    return out


# ---------------------------------------------------------------------------
# Comparação
# ---------------------------------------------------------------------------

OK, CHANGED, MISSING, WARN, SKIP = "ok", "mudou", "sumiu", "aviso", "n/v"


@dataclass
class Finding:
    section: str
    status: str
    symbol: str
    expected: str
    found: str
    where: str
    note: str = ""


def hx(v: Optional[int]) -> str:
    return "—" if v is None else f"0x{v:X}"


def expected_type(comment: str) -> Optional[str]:
    """Primeira palavra do comentário do header, se parecer um tipo C#: "int", "float[]", "Vector2"."""
    tok = comment.split()[0] if comment.split() else ""
    return tok if re.fullmatch(r"[A-Za-z_][\w.]*(\[\])?", tok) else None


def same_type(a: str, b: str) -> bool:
    short = lambda s: re.sub(r"^.*\.", "", s.replace(" ", ""))
    return short(a) == short(b)


def parse_const_int(text: Optional[str]) -> Optional[int]:
    if text is None:
        return None
    try:
        return int(text.strip().rstrip("uUlL"), 0)
    except ValueError:
        return None


def compare(dump: Dump, header_path: Path, header: List[HeaderConst],
            progress: List[Tuple[str, str, str, int]], progress_path: Path,
            ranges: List[Tuple[str, int, int, int, int]], registry_path: Path) -> List[Finding]:
    out: List[Finding] = []
    hname = header_path.name

    def where(c: HeaderConst) -> str:
        return f"{hname}:{c.line} {c.qualified}"

    by_ns: Dict[str, Dict[str, HeaderConst]] = {}
    for c in header:
        by_ns.setdefault(c.namespace, {})[c.name] = c
    top = by_ns.get("", {})

    # --- assembly e classes nomeadas -----------------------------------------
    if "kAssembly" in top and dump.images:
        c = top["kAssembly"]
        img = c.value.strip('"')
        out.append(Finding("classes", OK if img in dump.images else MISSING, f"imagem {img}",
                           img, img if img in dump.images else "não listada", where(c)))
    for name, ns in CLASS_NAME_CONSTANTS.items():
        if name in top:
            c = top[name]
            cls = c.value.strip('"')
            t = dump.get(ns, cls)
            out.append(Finding("classes", OK if t else MISSING, f"{ns + '.' if ns else ''}{cls}",
                               "existe", "existe" if t else "não encontrada", where(c)))

    # --- campos ----------------------------------------------------------------
    for group, (ns, cls, kind) in FIELD_GROUPS.items():
        for c in by_ns.get(group, {}).values():
            sym = f"{ns + '.' if ns else ''}{cls}.{c.name}"
            if not dump.has_fields:
                out.append(Finding("campos", SKIP, sym, hx(c.int_value), "—", where(c),
                                   "script.json não traz offsets de campo; passe --dump dump.cs"))
                continue
            t = dump.get(ns, cls)
            if t is None:
                out.append(Finding("campos", MISSING, sym, hx(c.int_value), "classe sumiu", where(c)))
                continue
            f, owner = dump.find_field(t, c.name)
            if f is None:
                out.append(Finding("campos", MISSING, sym, hx(c.int_value), "campo sumiu", where(c)))
                continue
            notes = []
            if owner is not t:
                notes.append(f"herdado de {owner.name}")
            if (kind == "static") != f.is_static:
                notes.append(f"esperado {'estático' if kind == 'static' else 'de instância'}")
            if f.thread_static:
                notes.append("agora é [ThreadStatic]")
            status = OK if f.offset == c.int_value else CHANGED
            exp_t = expected_type(c.comment)
            if exp_t and not same_type(exp_t, f.type):
                notes.append(f"tipo {exp_t} -> {f.type}")
                if status == OK:
                    status = WARN
            if status == OK and any(n.startswith("esperado") or "ThreadStatic" in n for n in notes):
                status = WARN
            out.append(Finding("campos", status, sym, hx(c.int_value), hx(f.offset), where(c),
                               "; ".join(notes)))

    # --- métodos (rva:: e argc::) ------------------------------------------------
    rvas = by_ns.get("rva", {})
    argcs = by_ns.get("argc", {})
    for key in list(rvas) + [k for k in argcs if k not in rvas]:
        prefix, _, meth = key.partition("_")
        if prefix not in METHOD_CLASSES or not meth:
            out.append(Finding("métodos", WARN, key, "", "", where(rvas.get(key) or argcs[key]),
                               f"sem regra para o prefixo '{prefix}' (METHOD_CLASSES)"))
            continue
        ns, cls = METHOD_CLASSES[prefix]
        sym = f"{ns}.{cls}.{meth}"
        rc, ac = rvas.get(key), argcs.get(key)
        ref = rc or ac
        t = dump.get(ns, cls)
        overloads = t.methods.get(meth, []) if t else []
        if t is None or not overloads:
            out.append(Finding("métodos", MISSING, sym, hx(rc.int_value) if rc else f"{ac.int_value} parâm.",
                               "classe sumiu" if t is None else "método sumiu", where(ref)))
            continue
        cands = overloads
        notes = []
        if ac is not None:
            cands = [o for o in overloads if o.params == ac.int_value]
            if not cands:
                got = ", ".join(str(o.params) for o in overloads)
                out.append(Finding("métodos", CHANGED, sym, f"{ac.int_value} parâm.", f"{got} parâm.",
                                   where(ac), "assinatura mudou: reconferir o typedef do hook"))
                continue
        if len(cands) > 1 and (ac is None or rc is None):
            notes.append(f"{len(cands)} overloads com esse nome; adicione argc::{key}" if ac is None
                         else f"{len(cands)} overloads com {ac.int_value} parâm.; o RVA desempata (rva::{key})")
        if rc is None:  # só argc: confere existência e número de parâmetros
            out.append(Finding("métodos", WARN if notes else OK, sym, f"{ac.int_value} parâm.",
                               f"{cands[0].params} parâm.", where(ac), "; ".join(notes)))
            continue
        found = cands[0]
        if len(cands) > 1:
            # Mesmo nome e mesmo nº de parâmetros (ex.: Draw(int, ...) e Draw(Layout, ...)):
            # o RVA diz qual é; se nenhum bate, a ponte precisa reconferir o overload.
            match = [o for o in cands if o.rva == rc.int_value]
            found = match[0] if match else found
            if not match and ac is not None:
                notes.append(f"{len(cands)} overloads com {ac.int_value} parâm. e nenhum no RVA esperado")
        status = OK if found.rva == rc.int_value else CHANGED
        if found.rva is None:
            notes.append("sem corpo (abstrato/RVA -1)")
            status = CHANGED
        if status == OK and notes:
            status = WARN
        out.append(Finding("métodos", status, sym, hx(rc.int_value), hx(found.rva), where(rc), "; ".join(notes)))

    # --- constantes do jogo espelhadas no header ---------------------------------
    for name, (ns, cls, fname) in CONST_CHECKS.items():
        if name not in top:
            continue
        c = top[name]
        sym = f"{ns}.{cls}.{fname}"
        if not dump.has_fields:
            out.append(Finding("constantes", SKIP, sym, c.value, "—", where(c), "precisa de dump.cs"))
            continue
        t = dump.get(ns, cls)
        f = t.fields.get(fname) if t else None
        if f is None:
            out.append(Finding("constantes", MISSING, sym, c.value, "sumiu", where(c)))
            continue
        val = parse_const_int(f.const_value)
        if val is None:
            out.append(Finding("constantes", WARN, sym, c.value, f"não é const ({f.type})", where(c),
                               "virou static readonly: só dá para ler em runtime"))
        else:
            out.append(Finding("constantes", OK if val == c.int_value else CHANGED, sym, c.value, str(val), where(c)))

    # --- referências por nome fora do header -------------------------------------
    pname = progress_path.name
    for flag, cls, fname, lineno in progress:
        sym = f"Terraria.{cls}.{fname}"
        loc = f"{pname}:{lineno} {flag}"
        if not dump.has_fields:
            out.append(Finding("nomes", SKIP, sym, "static bool", "—", loc, "precisa de dump.cs"))
            continue
        t = dump.get("Terraria", cls)
        f = t.fields.get(fname) if t else None
        if f is None:
            out.append(Finding("nomes", MISSING, sym, "static bool", "sumiu", loc,
                               "o Boss Checklist vai mostrar 'unknown' para essa entrada"))
        elif not f.is_static or f.type != "bool" or f.thread_static:
            out.append(Finding("nomes", CHANGED, sym, "static bool",
                               f"{'static ' if f.is_static else ''}{f.type}", loc))
        else:
            out.append(Finding("nomes", OK, sym, "static bool", f"static bool @{hx(f.offset)}", loc))

    rname = registry_path.name
    for kind, vanilla, first_mod, _max, lineno in ranges:
        ns, cls, fname = REGISTRY_KINDS[kind]
        sym = f"{ns}.{cls}.{fname}"
        loc = f"{rname}:{lineno} {kind}"
        if not dump.has_fields:
            out.append(Finding("contagens", SKIP, sym, str(vanilla), "—", loc, "precisa de dump.cs"))
            continue
        t = dump.get(ns, cls)
        f = t.fields.get(fname) if t else None
        if f is None:
            out.append(Finding("contagens", MISSING, sym, str(vanilla), "sumiu", loc))
            continue
        val = parse_const_int(f.const_value)
        if val is None:
            note = "static readonly: valor só existe em runtime"
            out.append(Finding("contagens", OK if vanilla < 0 else WARN, sym,
                               "runtime" if vanilla < 0 else str(vanilla), "runtime", loc, note))
            continue
        notes = []
        status = OK if val == vanilla else CHANGED
        if val >= first_mod:
            status = CHANGED
            notes.append(f"passou do 1º ID de mod ({first_mod}): conteúdo de mod desse tipo será desativado")
        elif status == CHANGED:
            notes.append(f"folga até o 1º ID de mod: {first_mod - val}")
        out.append(Finding("contagens", status, sym, str(vanilla), str(val), loc, "; ".join(notes)))
    return out


# ---------------------------------------------------------------------------
# Relatório
# ---------------------------------------------------------------------------

SECTIONS = [
    ("classes", "Classes e assembly"),
    ("campos", "Campos (terraria_offsets.h: *_off)"),
    ("métodos", "Métodos (terraria_offsets.h: rva::, argc::)"),
    ("constantes", "Constantes do jogo (terraria_offsets.h)"),
    ("nomes", "Flags lidas por nome (Boss Checklist)"),
    ("contagens", "IDs originais (content_registry.cpp)"),
]
LABEL = {OK: "OK   ", CHANGED: "MUDOU", MISSING: "SUMIU", WARN: "AVISO", SKIP: "N/V  "}


def render(findings: List[Finding], show_all: bool, sources: List[str], header: Path) -> str:
    lines = [
        "TLMod offset diff",
        f"  registrado: {header}",
        f"  dump:       {', '.join(sources)}",
        "",
    ]
    for key, title in SECTIONS:
        items = [f for f in findings if f.section == key]
        if not items:
            continue
        shown = items if show_all else [f for f in items if f.status != OK]
        counts = {s: sum(1 for f in items if f.status == s) for s in LABEL}
        summary = ", ".join(f"{n} {s}" for s, n in counts.items() if n)
        lines.append(f"== {title}  [{summary}]")
        if not shown:
            lines.append("   (tudo igual)")
        w = max(len(f.symbol) for f in shown) if shown else 0
        for f in shown:
            change = f.expected if f.status == OK else f"{f.expected} -> {f.found}"
            row = f"   {LABEL[f.status]}  {f.symbol.ljust(w)}  {change}"
            if f.note:
                row += f"   ({f.note})"
            lines.append(row)
            if f.status != OK:
                lines.append(f"   {'':5}  {'':{w}}  em {f.where}")
        lines.append("")

    bad = [f for f in findings if f.status in (CHANGED, MISSING)]
    warns = [f for f in findings if f.status == WARN]
    skipped = [f for f in findings if f.status == SKIP]
    total = len(findings)
    lines.append(f"Resumo: {total} verificados, {total - len(bad) - len(warns) - len(skipped)} iguais, "
                 f"{sum(f.status == CHANGED for f in findings)} mudaram, "
                 f"{sum(f.status == MISSING for f in findings)} sumiram, {len(warns)} avisos"
                 + (f", {len(skipped)} não verificáveis" if skipped else ""))
    if bad or warns:
        lines.append("")
        lines.append("Reconferir:")
        for f in bad + warns:
            note = f"; {f.note}" if f.note else ""
            lines.append(f"  - {f.symbol}: {f.expected} -> {f.found} ({f.where}{note})")
    elif skipped:
        lines.append(f"Nada mudou no que foi verificado, mas {len(skipped)} itens não puderam ser "
                     "conferidos (script.json não traz campos): rode também com --dump dump.cs.")
    else:
        lines.append("Nada a reconferir: o dump bate com o que o TLMod registra.")
    return "\n".join(lines)


def main(argv: Optional[Iterable[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", type=Path, help="dump.cs do Il2CppDumper (campos e métodos)")
    ap.add_argument("--script", type=Path, help="script.json do Il2CppDumper (só métodos)")
    ap.add_argument("--offsets", type=Path, default=DEFAULT_HEADER, help="terraria_offsets.h")
    ap.add_argument("--progress-src", type=Path, default=DEFAULT_PROGRESS_SRC,
                    help="fonte com as flags do Boss Checklist (bridge_adapter.cpp)")
    ap.add_argument("--registry-src", type=Path, default=DEFAULT_REGISTRY_SRC,
                    help="fonte com as faixas de ID (content_registry.cpp)")
    ap.add_argument("--all", action="store_true", help="lista também o que não mudou")
    ap.add_argument("--json", action="store_true", help="saída em JSON (para CI)")
    args = ap.parse_args(list(argv) if argv is not None else None)

    if not args.dump and not args.script:
        ap.error("informe --dump dump.cs e/ou --script script.json")
    try:
        dump = Dump()
        if args.dump:
            parse_dump_cs(args.dump, dump)
        if args.script:
            parse_script_json(args.script, dump)
        header = parse_header(args.offsets)
    except (OSError, ValueError) as e:
        print(f"erro: {e}", file=sys.stderr)
        return 2
    if not header:
        print(f"erro: nenhuma constante encontrada em {args.offsets}", file=sys.stderr)
        return 2

    findings = compare(dump, args.offsets, header, parse_progress_fields(args.progress_src), args.progress_src,
                       parse_registry_ranges(args.registry_src), args.registry_src)
    if args.json:
        print(json.dumps([f.__dict__ for f in findings], ensure_ascii=False, indent=2))
    else:
        print(render(findings, args.all, dump.sources, args.offsets))
    return 1 if any(f.status in (CHANGED, MISSING, WARN) for f in findings) else 0


if __name__ == "__main__":
    sys.exit(main())
