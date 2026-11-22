"""What gen_stub.py emits from an interface table: vtable slots, the stub kind per return type, the GUID initializer and the exports.

The table comes from idl_table.py over a synthetic IDL, so the two generators are checked together without the Xodus IDLs. The emitted
C++ is not compiled: these assertions cover the text the run_tests.py build consumes, not the behaviour of the built DLL.
"""

import json
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

XGR = pathlib.Path(__file__).resolve().parents[1] / "xbox/platform/xgr"

IDL = """
[
    uuid(11111111-2222-3333-4444-555555555555)
]
interface IXSampleImpl : IUnknown
{
    HRESULT STDMETHODCALLTYPE XSampleFirst( [in] UINT32 value );
    HRESULT __PADDING__();
    UINT32 XSampleCount();
    void XSampleReset();
    XSampleHandle XSampleGetHandle( );
}

[
    uuid(66666666-7777-8888-9999-aaaaaaaaaaaa)
]
interface IXSampleImpl2 : IXSampleImpl
{
    HRESULT XSampleSecond( [out] UINT32 *value );
}
"""

SLOT_LINE = re.compile(r'^\s*\{"([^"]+)", "([^"]+)", (\d+)\},$')
IFACE_LINE = re.compile(r'^\s*\{"([^"]+)", (\{.+\}), (\d+), (\d+)\},$')
UUID = "11111111-2222-3333-4444-555555555555"


def run(script, *args):
    return subprocess.run([sys.executable, "-I", str(XGR / script), *map(str, args)],
                          capture_output=True, text=True)


def slots_of(source):
    return [(m.group(1), m.group(2), int(m.group(3)))
            for m in (SLOT_LINE.match(line) for line in source.splitlines()) if m]


def interfaces_of(source):
    return [(m.group(1), m.group(2), int(m.group(3)), int(m.group(4)))
            for m in (IFACE_LINE.match(line) for line in source.splitlines()) if m]


class GeneratedStubTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.directory.name)
        (root / "xsample.idl").write_text(IDL, encoding="utf-8")
        cls.idl = run("idl_table.py", root, root / "table.json")
        cls.gen = run("gen_stub.py", root / "table.json", root / "gen")
        cls.source = (root / "gen/xgr_stub.cpp").read_text(encoding="utf-8")
        cls.defs = (root / "gen/xgameruntime.def").read_text(encoding="utf-8")
        cls.slots = slots_of(cls.source)
        cls.interfaces = interfaces_of(cls.source)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_both_generators_succeed(self):
        self.assertEqual(self.idl.returncode, 0, self.idl.stderr)
        self.assertEqual(self.gen.returncode, 0, self.gen.stderr)
        self.assertIn("2 interfaces, 17 vtable slots", self.gen.stdout)

    def test_unknown_methods_are_the_first_three_slots(self):
        for interface in ("IXSampleImpl", "IXSampleImpl2"):
            self.assertEqual([(name, kind) for owner, name, kind in self.slots if owner == interface][:3],
                             [("QueryInterface", 0), ("AddRef", 0), ("Release", 0)])

    def test_methods_start_at_the_fourth_slot(self):
        self.assertEqual(self.slots[3], ("IXSampleImpl", "XSampleFirst", 1))

    def test_a_padding_method_keeps_its_own_slot(self):
        self.assertEqual(self.slots[4], ("IXSampleImpl", "__PADDING__", 1))
        self.assertEqual(self.slots[5], ("IXSampleImpl", "XSampleCount", 2))

    def test_return_type_selects_the_stub_kind(self):
        self.assertEqual([(name, kind) for owner, name, kind in self.slots if owner == "IXSampleImpl"][6:],
                         [("XSampleReset", 2), ("XSampleGetHandle", 3)])

    def test_stdmethodcalltype_is_not_part_of_the_return_type(self):
        self.assertEqual(self.slots[3][2], 1)

    def test_a_derived_interface_repeats_its_parent_slots(self):
        own = [(name, kind) for owner, name, kind in self.slots if owner == "IXSampleImpl2"]
        self.assertEqual(own, [("QueryInterface", 0), ("AddRef", 0), ("Release", 0), ("XSampleFirst", 1),
                               ("__PADDING__", 1), ("XSampleCount", 2), ("XSampleReset", 2),
                               ("XSampleGetHandle", 3), ("XSampleSecond", 1)])

    def test_interface_offsets_follow_the_slot_order(self):
        self.assertEqual([(name, first, count) for name, _, first, count in self.interfaces],
                         [("IXSampleImpl", 0, 8), ("IXSampleImpl2", 8, 9)])

    def test_the_offsets_cover_the_slot_table(self):
        self.assertEqual(sum(count for _, _, _, count in self.interfaces), len(self.slots))

    def test_guid_initializer_splits_the_uuid_into_fields(self):
        self.assertIn('{"IXSampleImpl2", {0x66666666, 0x7777, 0x8888, '
                      '{0x99, 0x99, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa}}, 8, 9},', self.source)

    def test_the_definition_file_exports_the_entry_points(self):
        lines = self.defs.splitlines()
        self.assertEqual(lines[:8],
                         ["LIBRARY xgameruntime", "EXPORTS", "    QueryApiImpl", "    InitializeApiImpl",
                          "    InitializeApiImplEx", "    InitializeApiImplEx2", "    UninitializeApiImpl",
                          "    XErrorReport"])
        self.assertEqual(len(lines), len(set(lines)))

    def test_a_missing_output_directory_is_created(self):
        self.assertTrue((pathlib.Path(self.directory.name) / "gen").is_dir())


class TableOnlyTest(unittest.TestCase):
    def generate(self, interfaces):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "table.json").write_text(json.dumps({"interfaces": interfaces}), encoding="utf-8")
            result = run("gen_stub.py", root / "table.json", root / "gen")
            self.assertEqual(result.returncode, 0, result.stderr)
            return slots_of((root / "gen/xgr_stub.cpp").read_text(encoding="utf-8"))

    def test_a_slot_without_a_method_is_named_by_its_number(self):
        slots = self.generate([{"name": "IXGap", "uuid": UUID, "parent": "IUnknown", "nslots": 6,
                                "methods": [{"name": "XGapFirst", "ret": "HRESULT", "slot": 3}]}])
        self.assertEqual(slots[3:], [("IXGap", "XGapFirst", 1), ("IXGap", "slot4", 1), ("IXGap", "slot5", 1)])

    def test_a_parent_outside_the_table_ends_the_inheritance_chain(self):
        slots = self.generate([{"name": "IXOrphan", "uuid": UUID, "parent": "IMissing", "nslots": 5,
                                "methods": [{"name": "XOrphan", "ret": "UINT32", "slot": 3}]}])
        self.assertEqual(slots[3:], [("IXOrphan", "XOrphan", 2), ("IXOrphan", "slot4", 1)])

    def test_scalar_returns_take_the_zero_kind(self):
        methods = [{"name": "XScalar%d" % index, "ret": ret, "slot": 3 + index}
                   for index, ret in enumerate(("UINT32", "UINT64", "size_t", "BOOLEAN", "bool", "INT64", "float", "void"))]
        slots = self.generate([{"name": "IXScalar", "uuid": UUID, "parent": "IUnknown", "nslots": 11,
                                "methods": methods}])
        self.assertEqual({kind for _, _, kind in slots[3:]}, {2})

    def test_a_struct_return_takes_the_pointer_kind(self):
        slots = self.generate([{"name": "IXStruct", "uuid": UUID, "parent": "IUnknown", "nslots": 4,
                                "methods": [{"name": "XStruct", "ret": "XStructResult", "slot": 3}]}])
        self.assertEqual(slots[3], ("IXStruct", "XStruct", 3))


if __name__ == "__main__":
    unittest.main()
