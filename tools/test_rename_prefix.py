#!/usr/bin/env python3
"""Testes do rename_prefix.py num repositório git temporário.

    python3 tools/test_rename_prefix.py -v
"""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rename_prefix as rp  # noqa: E402


def sh(root, *args):
    subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)


class RenamePrefixTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        files = {
            "proj/include/acme/acme_api.h": "#define ACME_API_VERSION 1\ntypedef struct AcmeCtx AcmeCtx;\n"
                                            "int acme_mod_init(AcmeCtx*);  // Acme: ação\n",
            "proj/java/com/acme/AcmeView.java": "package com.acme;\nclass AcmeView {}\n",
            "proj/mods/demo/manifest.json": '{"id": "acme.demo", "acme_api": 1}\n',
            "proj/plain.txt": "nada aqui\n",
            "proj/blob.bin": "\0\1\2sem prefixo",
            "other/site.html": "<title>Acme site</title>\n",   # fora do escopo
        }
        for rel, text in files.items():
            p = self.root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(text.encode("utf-8"))
        sh(self.root, "init", "-q")
        sh(self.root, "add", ".")
        sh(self.root, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "base")

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = rp.main([*args, "--root", str(self.root), "--path", "proj"])
        return rc, out.getvalue(), err.getvalue()

    def read(self, rel):
        return (self.root / rel).read_text(encoding="utf-8")

    def test_troca_cada_grafia_e_conta(self):
        rc, out, _ = self.run_tool("Acme", "Zed")
        self.assertEqual(rc, 0)
        h = self.read("proj/include/zed/zed_api.h")
        self.assertIn("#define ZED_API_VERSION 1", h)
        self.assertIn("typedef struct ZedCtx ZedCtx;", h)
        self.assertIn("int zed_mod_init(ZedCtx*);  // Zed: ação", h)   # UTF-8 preservado
        self.assertEqual(self.read("proj/mods/demo/manifest.json"), '{"id": "zed.demo", "zed_api": 1}\n')
        # cabeçalho: ACME 1, Acme 4, acme 1; Java: Acme 1, acme 1; manifest: acme 2
        self.assertIn("3 arquivos alterados, 10 ocorrências (ACME: 1, Acme: 5, acme: 4), 2 arquivos movidos", out)

    def test_move_arquivos_e_pastas_com_git_mv(self):
        self.run_tool("Acme", "Zed")
        self.assertTrue((self.root / "proj/java/com/zed/ZedView.java").is_file())
        self.assertFalse((self.root / "proj/java/com/acme").exists())   # pasta vazia removida
        self.assertFalse((self.root / "proj/include/acme").exists())
        status = subprocess.run(["git", "status", "--porcelain"], cwd=self.root, capture_output=True,
                                text=True).stdout
        # R = renomeado no índice (histórico acompanha); M = conteúdo também trocado
        self.assertIn("RM proj/java/com/acme/AcmeView.java -> proj/java/com/zed/ZedView.java", status)

    def test_fora_do_escopo_nao_muda(self):
        self.run_tool("Acme", "Zed")
        self.assertEqual(self.read("other/site.html"), "<title>Acme site</title>\n")
        self.assertEqual((self.root / "proj/blob.bin").read_bytes(), b"\0\1\2sem prefixo")

    def test_dry_run_nao_muda_nada(self):
        rc, out, _ = self.run_tool("Acme", "Zed", "--dry-run")
        self.assertEqual(rc, 0)
        self.assertIn("seriam alterados", out)
        self.assertTrue((self.root / "proj/include/acme/acme_api.h").is_file())
        self.assertIn("AcmeCtx", self.read("proj/include/acme/acme_api.h"))

    def test_grafia_imprevista_recusa_sem_mudar(self):
        (self.root / "proj/plain.txt").write_text("AcMe\n", encoding="utf-8")
        rc, _, err = self.run_tool("Acme", "Zed")
        self.assertEqual(rc, 2)
        self.assertIn("proj/plain.txt:1: grafia não prevista 'AcMe'", err)
        self.assertIn("AcmeCtx", self.read("proj/include/acme/acme_api.h"))

    def test_nome_novo_ja_usado_recusa(self):
        (self.root / "proj/plain.txt").write_text("zed\n", encoding="utf-8")
        rc, _, err = self.run_tool("Acme", "Zed")
        self.assertEqual(rc, 2)
        self.assertIn("já contém 'Zed'", err)

    def test_binario_com_prefixo_recusa(self):
        (self.root / "proj/blob.bin").write_bytes(b"\0acme")
        rc, _, err = self.run_tool("Acme", "Zed")
        self.assertEqual(rc, 2)
        self.assertIn("proj/blob.bin: arquivo binário/não UTF-8 contém o prefixo", err)

    def test_nome_invalido_recusa(self):
        for bad in ("1Zed", "Ze-d", ""):
            rc, _, _ = self.run_tool("Acme", bad)
            self.assertEqual(rc, 2, bad)

    def test_ida_e_volta_restaura_o_original(self):
        before = {p: p.read_bytes() for p in (self.root / "proj").rglob("*") if p.is_file()}
        self.assertEqual(self.run_tool("Acme", "Zed")[0], 0)
        self.assertEqual(self.run_tool("Zed", "Acme")[0], 0)
        after = {p: p.read_bytes() for p in (self.root / "proj").rglob("*") if p.is_file()}
        self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
