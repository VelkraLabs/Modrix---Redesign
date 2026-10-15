#!/usr/bin/env python3
"""Testes do offset_diff.py com um dump.cs/script.json sintético (formato Il2CppDumper).

    python3 tools/test_offset_diff.py -v
"""

import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import offset_diff as od  # noqa: E402

HEADER = textwrap.dedent("""\
    namespace terraria {
    constexpr const char* kAssembly = "Assembly-CSharp.dll";
    constexpr const char* kPlayerClass = "Player";
    constexpr int32_t kNpcVanillaCount = 688;
    namespace player_off {
    constexpr size_t whoAmI = 0x10;        // int   (herdado de Entity)
    constexpr size_t statLife = 0x704;     // int
    constexpr size_t statDefense = 0x6F8;  // int
    }  // namespace player_off
    namespace main_static_off {
    constexpr size_t player = 0x678;   // Player[] Main.player
    }  // namespace main_static_off
    namespace rva {
    constexpr uintptr_t Player_Hurt = 0xFA15B8;
    constexpr uintptr_t Player_Update = 0xF5F9A8;
    }  // namespace rva
    namespace argc {
    constexpr int Player_Hurt = 2;
    constexpr int Main_get_myPlayer = 0;
    }  // namespace argc
    }  // namespace terraria
    """)

DUMP = textwrap.dedent("""\
    // Image 0: Assembly-CSharp.dll - 0
    // Image 1: mscorlib.dll - 100

    // Namespace: Terraria
    public interface IFixLoadedData // TypeDefIndex: 1
    {
    }

    // Namespace: Terraria
    public abstract class Entity // TypeDefIndex: 2
    {
    \t// Fields
    \tpublic int whoAmI; // 0x10
    }

    // Namespace: Terraria
    public class Player : Entity, IFixLoadedData // TypeDefIndex: 3
    {
    \t// Fields
    \tpublic int statDefense; // 0x6F8
    \tpublic int statLife; // 0x704

    \t// Methods

    \t// RVA: 0xFA15B8 Offset: 0xFA15B8 VA: 0xFA15B8
    \tpublic double Hurt(PlayerDeathReason damageSource, int Damage) { }

    \t// RVA: 0xF5F9A8 Offset: 0xF5F9A8 VA: 0xF5F9A8
    \tpublic void Update(int i) { }
    }

    // Namespace: Terraria
    public class Main // TypeDefIndex: 4
    {
    \t// Fields
    \tpublic const int maxNPCs = 200;
    \tpublic static Player[] player; // 0x678

    \t// Methods

    \t// RVA: 0xC68B64 Offset: 0xC68B64 VA: 0xC68B64
    \tpublic static int get_myPlayer() { }
    }

    // Namespace: Terraria.ID
    public class NPCID // TypeDefIndex: 5
    {
    \t// Fields
    \tpublic const short Count = 688;
    }
    """)


class OffsetDiffTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.header = self.tmp / "terraria_offsets.h"
        self.header.write_text(HEADER, encoding="utf-8")
        # Fontes extras vazias: os testes de nomes usam as suas próprias.
        self.progress = self.tmp / "bridge_adapter.cpp"
        self.registry = self.tmp / "content_registry.cpp"
        self.progress.write_text("", encoding="utf-8")
        self.registry.write_text("", encoding="utf-8")

    def run_diff(self, dump_text=None, script=None):
        dump = od.Dump()
        if dump_text is not None:
            p = self.tmp / "dump.cs"
            p.write_text(dump_text, encoding="utf-8")
            od.parse_dump_cs(p, dump)
        if script is not None:
            p = self.tmp / "script.json"
            p.write_text(json.dumps(script), encoding="utf-8")
            od.parse_script_json(p, dump)
        findings = od.compare(dump, self.header, od.parse_header(self.header),
                              od.parse_progress_fields(self.progress), self.progress,
                              od.parse_registry_ranges(self.registry), self.registry)
        return {f.symbol: f for f in findings}

    def test_header_parsing(self):
        consts = {c.qualified: c for c in od.parse_header(self.header)}
        self.assertEqual(consts["player_off::statLife"].int_value, 0x704)
        self.assertEqual(consts["rva::Player_Hurt"].int_value, 0xFA15B8)
        self.assertEqual(consts["kAssembly"].value, '"Assembly-CSharp.dll"')
        self.assertEqual(od.expected_type(consts["main_static_off::player"].comment), "Player[]")

    def test_identical_dump_is_all_ok(self):
        r = self.run_diff(DUMP)
        bad = {k: f.status for k, f in r.items() if f.status != od.OK}
        self.assertEqual(bad, {})
        # whoAmI vem da classe-base (Entity), não de IFixLoadedData.
        self.assertIn("herdado de Entity", r["Terraria.Player.whoAmI"].note)

    def test_offset_change_and_type_change(self):
        dump = DUMP.replace("statLife; // 0x704", "statLife; // 0x70C")
        dump = dump.replace("public int statDefense", "public DefenseStat statDefense")
        r = self.run_diff(dump)
        self.assertEqual(r["Terraria.Player.statLife"].status, od.CHANGED)
        self.assertEqual(r["Terraria.Player.statLife"].found, "0x70C")
        self.assertEqual(r["Terraria.Player.statDefense"].status, od.WARN)
        self.assertIn("DefenseStat", r["Terraria.Player.statDefense"].note)

    def test_missing_field_class_and_method(self):
        dump = DUMP.replace("\tpublic int statLife; // 0x704\n", "")
        dump = dump.replace("public void Update(int i)", "public void Tick(int i)")
        r = self.run_diff(dump)
        self.assertEqual(r["Terraria.Player.statLife"].status, od.MISSING)
        self.assertEqual(r["Terraria.Player.Update"].status, od.MISSING)
        r = self.run_diff(DUMP.replace("public class Main ", "public class MainGame "))
        self.assertEqual(r["Terraria.Main.player"].found, "classe sumiu")

    def test_rva_and_signature_change(self):
        r = self.run_diff(DUMP.replace("RVA: 0xFA15B8", "RVA: 0xFA2000"))
        self.assertEqual(r["Terraria.Player.Hurt"].status, od.CHANGED)
        self.assertEqual(r["Terraria.Player.Hurt"].found, "0xFA2000")
        r = self.run_diff(DUMP.replace("int Damage) { }", "int Damage, bool crit = False) { }"))
        self.assertEqual(r["Terraria.Player.Hurt"].status, od.CHANGED)
        self.assertIn("assinatura", r["Terraria.Player.Hurt"].note)

    def test_const_change(self):
        r = self.run_diff(DUMP.replace("Count = 688", "Count = 700"))
        self.assertEqual(r["Terraria.ID.NPCID.Count"].status, od.CHANGED)
        self.assertEqual(r["Terraria.ID.NPCID.Count"].found, "700")

    def test_name_references(self):
        self.progress.write_text('    {TLMOD_DOWNED_X, "Main", "hardMode"},\n', encoding="utf-8")
        self.registry.write_text('        {"npc", 688, 4096, 32767},\n', encoding="utf-8")
        r = self.run_diff(DUMP)
        self.assertEqual(r["Terraria.Main.hardMode"].status, od.MISSING)
        self.assertEqual(r["Terraria.ID.NPCID.Count"].status, od.OK)
        # Contagem passando do 1º ID de mod: alerta específico.
        r = self.run_diff(DUMP.replace("Count = 688", "Count = 5000"))
        hit = [f for f in r.values() if f.section == "contagens"][0]
        self.assertEqual(hit.status, od.CHANGED)
        self.assertIn("desativado", hit.note)

    def test_script_json_only_checks_methods(self):
        script = {"ScriptMethod": [
            {"Address": 0xFA15B8, "Name": "Terraria.Player$$Hurt",
             "Signature": "double Terraria_Player__Hurt (Terraria_Player_o* __this, "
                          "Terraria_DataStructures_PlayerDeathReason_o* damageSource, int32_t Damage, "
                          "const MethodInfo* method);"},
            {"Address": 0xF5F9A8, "Name": "Terraria.Player$$Update",
             "Signature": "void Terraria_Player__Update (Terraria_Player_o* __this, int32_t i, "
                          "const MethodInfo* method);"},
            {"Address": 0xC68B64, "Name": "Terraria.Main$$get_myPlayer",
             "Signature": "int32_t Terraria_Main__get_myPlayer (const MethodInfo* method);"},
        ]}
        r = self.run_diff(script=script)
        self.assertEqual(r["Terraria.Player.Hurt"].status, od.OK)
        self.assertEqual(r["Terraria.Main.get_myPlayer"].status, od.OK)
        self.assertEqual(r["Terraria.Player.statLife"].status, od.SKIP)

    def test_param_counting(self):
        self.assertEqual(od.count_params(""), 0)
        self.assertEqual(od.count_params("Dictionary<int, string> d, int x = 1"), 2)
        self.assertEqual(od.count_params('string s = "a,b", int[] xs'), 2)
        self.assertEqual(od.c_signature_params(
            "void F (Terraria_NPC_o* __this, int32_t Type, Terraria_NPCSpawnParams_o p, const MethodInfo* method);"), 2)

    def test_save_rules_namespaces_and_overloads(self):
        # Hooks de save: WorldFile fica em Terraria.IO e SaveWorld tem 2 overloads.
        header = HEADER.replace("namespace rva {", textwrap.dedent("""\
            namespace file_data_off {
            constexpr size_t _path = 0x10;         // string
            }  // namespace file_data_off
            namespace item_off {
            constexpr size_t netID = 0x198;     // int
            }  // namespace item_off
            namespace rva {
            constexpr uintptr_t WorldFile_SaveWorld = 0x14F15E8;"""))
        self.header.write_text(header, encoding="utf-8")
        dump = DUMP + textwrap.dedent("""\

            // Namespace: Terraria.IO
            public abstract class FileData // TypeDefIndex: 6
            {
            \t// Fields
            \tprotected string _path; // 0x10
            }

            // Namespace: Terraria
            public class Item // TypeDefIndex: 7
            {
            \t// Fields
            \tpublic int netID; // 0x198
            }

            // Namespace: Terraria.IO
            public class WorldFile // TypeDefIndex: 8
            {
            \t// Methods

            \t// RVA: 0x14ED6C8 Offset: 0x14ED6C8 VA: 0x14ED6C8
            \tpublic static void SaveWorld(WorldFile.WorldSaveContext saveContext = 0) { }

            \t// RVA: 0x14F15E8 Offset: 0x14F15E8 VA: 0x14F15E8
            \tpublic static void SaveWorld(bool useCloudSaving, bool resetTime = False, WorldFile.WorldSaveContext saveContext = 0) { }
            }
            """)
        r = self.run_diff(dump)
        self.assertEqual(r["Terraria.IO.FileData._path"].status, od.OK)
        self.assertEqual(r["Terraria.Item.netID"].status, od.OK)
        self.assertEqual(r["Terraria.IO.WorldFile.SaveWorld"].status, od.WARN)  # falta argc::
        self.assertIn("argc::WorldFile_SaveWorld", r["Terraria.IO.WorldFile.SaveWorld"].note)
        self.header.write_text(header.replace("namespace argc {",
                                              "namespace argc {\nconstexpr int WorldFile_SaveWorld = 3;"),
                               encoding="utf-8")
        self.assertEqual(self.run_diff(dump)["Terraria.IO.WorldFile.SaveWorld"].status, od.OK)

    def test_real_header_parses(self):
        consts = od.parse_header(od.DEFAULT_HEADER)
        groups = {c.namespace for c in consts}
        self.assertTrue({"player_off", "npc_off", "rva", "argc", "item_off", "chest_off"} <= groups)
        # Todo namespace de offsets do header precisa de uma regra no offset_diff.
        offset_groups = {g for g in groups if g.endswith("_off")}
        self.assertEqual(offset_groups - set(od.FIELD_GROUPS), set())
        prefixes = {c.name.partition("_")[0] for c in consts if c.namespace in ("rva", "argc")}
        self.assertEqual(prefixes - set(od.METHOD_CLASSES), set())


if __name__ == "__main__":
    unittest.main()
