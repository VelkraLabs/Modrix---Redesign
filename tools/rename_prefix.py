#!/usr/bin/env python3
"""Troca o prefixo (nome provisório) do projeto em conteúdo, arquivos e pastas.

    python3 tools/rename_prefix.py Atual Novo --dry-run   # só mostra o plano
    python3 tools/rename_prefix.py Atual Novo             # aplica

Os prefixos são dados na grafia de tipo (ex.: Acme -> Zed). As três grafias
usadas no código são derivadas deles, e cada uma vira a correspondente:

    Acme -> Zed   tipos C/C++, classes Java, funções da ponte, textos e logs
    acme -> zed   arquivos, funções exportadas, ids de mod, pacote Java, JSON
    ACME -> ZED   macros, variáveis do CMake

A troca é por substring (AcmeContext, acme_mod_init, libacme_bridge...). Se
aparecer qualquer outra mistura de maiúsculas, o script para sem mudar nada:
quem decide é uma pessoa. Este arquivo fica fora da troca e não cita o nome
atual, para continuar valendo depois de cada renomeação.

Escopo: arquivos versionados no git e arquivos novos não ignorados, só dentro
de PROJECT_PATHS. O resto do repositório pertence a outro projeto e nunca é
tocado. Pastas e arquivos com o prefixo no nome são movidos com `git mv`,
para o histórico acompanhar.

Depois de aplicar, recompile em pastas de build novas: as variáveis do CMake
e os caminhos de include mudam junto.

Códigos de saída: 0 ok (ou nada a fazer), 1 sobrou ocorrência, 2 erro/recusa.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

# Pastas/arquivos que são desta ferramenta (relativos à raiz do repositório).
PROJECT_PATHS = ["il2cpp_bridge", "modapi", "tools", "player_fields.md"]

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
SELF = Path(__file__).resolve()


class RenameError(Exception):
    pass


def variants(old, new):
    """{grafia antiga: grafia nova} para as três grafias."""
    table = {old.lower(): new.lower(), old.upper(): new.upper(), old: new}
    return table


def git(root, *args):
    r = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)
    if r.returncode != 0:
        raise RenameError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def scope_files(root, paths):
    """Arquivos versionados + novos não ignorados, dentro de `paths`."""
    out = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *paths)
    files = sorted({p for p in out.split("\0") if p})
    return [f for f in files if (root / f).is_file() and (root / f).resolve() != SELF]


def plan(root, old, new, paths):
    """Monta o plano inteiro antes de mudar qualquer coisa."""
    table = variants(old, new)
    pattern = re.compile(re.escape(old), re.IGNORECASE)
    edits = {}            # arquivo -> (texto novo, Counter por grafia)
    moves = []            # (caminho antigo, caminho novo)
    problems = []

    def substitute(text, where, counter):
        def repl(m):
            s = m.group(0)
            if s not in table:
                problems.append(f"{where}: grafia não prevista {s!r}")
                return s
            counter[s] += 1
            return table[s]
        return pattern.sub(repl, text)

    files = scope_files(root, paths)
    for rel in files:
        data = (root / rel).read_bytes()
        try:
            if b"\0" in data:
                raise UnicodeDecodeError("bin", data, 0, 1, "binário")
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            if pattern.search(data.decode("latin-1")):
                problems.append(f"{rel}: arquivo binário/não UTF-8 contém o prefixo")
            continue
        if not pattern.search(text):
            continue
        counter = Counter()
        lines = text.split("\n")
        new_lines = [substitute(line, f"{rel}:{i}", counter) for i, line in enumerate(lines, 1)]
        edits[rel] = ("\n".join(new_lines), counter)

    for rel in files:
        counter = Counter()
        new_rel = substitute(rel, f"caminho {rel}", counter)
        if new_rel != rel:
            if (root / new_rel).exists():
                problems.append(f"caminho {rel}: destino {new_rel} já existe")
            moves.append((rel, new_rel))

    # O nome novo já em uso deixaria a troca ambígua de desfazer.
    if old.lower() != new.lower():
        clash = re.compile(re.escape(new), re.IGNORECASE)
        for rel in files:
            try:
                text = (root / rel).read_text(encoding="utf-8")
            except (UnicodeDecodeError, ValueError):
                text = ""
            if clash.search(rel) or clash.search(text):
                problems.append(f"{rel}: já contém {new!r} (qualquer grafia)")
    return edits, moves, problems


def remaining(root, old, paths):
    pattern = re.compile(re.escape(old), re.IGNORECASE)
    hits = []
    for rel in scope_files(root, paths):
        if pattern.search(rel):
            hits.append(f"caminho {rel}")
        try:
            text = (root / rel).read_text(encoding="utf-8")
        except (UnicodeDecodeError, ValueError):
            continue
        hits += [f"{rel}:{i}" for i, line in enumerate(text.split("\n"), 1) if pattern.search(line)]
    return hits


def apply(root, edits, moves):
    for rel, (text, _) in edits.items():
        (root / rel).write_bytes(text.encode("utf-8"))
    tracked = set(git(root, "ls-files", "-z").split("\0"))
    old_dirs = set()
    for rel, new_rel in moves:
        (root / new_rel).parent.mkdir(parents=True, exist_ok=True)
        if rel in tracked:
            git(root, "mv", "--", rel, new_rel)
        else:
            os.rename(root / rel, root / new_rel)
        old_dirs.update(Path(rel).parents)
    # Pastas antigas que ficaram vazias (git não guarda pastas).
    for d in sorted((root / d for d in old_dirs if str(d) != "."), key=lambda p: len(p.parts), reverse=True):
        try:
            d.rmdir()
        except OSError:
            pass


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("old", help="prefixo atual, grafia de tipo (ex.: Acme)")
    ap.add_argument("new", help="prefixo novo, grafia de tipo (ex.: Zed)")
    ap.add_argument("--dry-run", action="store_true", help="mostra o plano e não muda nada")
    ap.add_argument("--root", type=Path, default=SELF.parent.parent, help="raiz do repositório")
    ap.add_argument("--path", action="append", dest="paths",
                    help=f"limita o escopo (repetível). Padrão: {' '.join(PROJECT_PATHS)}")
    ap.add_argument("-v", "--verbose", action="store_true", help="lista cada arquivo")
    args = ap.parse_args(argv)
    root = args.root.resolve()
    paths = args.paths or PROJECT_PATHS

    try:
        for name in (args.old, args.new):
            if not NAME_RE.match(name):
                raise RenameError(f"{name!r}: use só letras e dígitos, começando por letra")
        if args.old == args.new:
            raise RenameError("prefixo novo igual ao atual")
        edits, moves, problems = plan(root, args.old, args.new, paths)
        if problems:
            raise RenameError("nada foi alterado:\n  " + "\n  ".join(problems))
    except RenameError as e:
        print(f"rename_prefix: {e}", file=sys.stderr)
        return 2

    total = Counter()
    for rel, (_, counter) in sorted(edits.items()):
        total.update(counter)
        if args.verbose or args.dry_run:
            print(f"  {sum(counter.values()):4d}  {rel}")
    for rel, new_rel in moves:
        if args.verbose or args.dry_run:
            print(f"  mover {rel} -> {new_rel}")
    by_case = ", ".join(f"{k}: {v}" for k, v in sorted(total.items()))
    verb = "seriam alterados" if args.dry_run else "alterados"
    print(f"{len(edits)} arquivos {verb}, {sum(total.values())} ocorrências ({by_case or 'nenhuma'}), "
          f"{len(moves)} arquivos movidos")
    if args.dry_run:
        return 0

    try:
        apply(root, edits, moves)
    except RenameError as e:
        print(f"rename_prefix: {e}", file=sys.stderr)
        return 2
    left = remaining(root, args.old, paths)
    if left:
        print(f"sobrou {args.old!r} em:\n  " + "\n  ".join(left), file=sys.stderr)
        return 1
    print(f"nenhuma ocorrência de {args.old!r} (qualquer grafia) no escopo: {' '.join(paths)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
