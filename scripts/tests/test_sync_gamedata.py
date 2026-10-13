"""Behavior tests for scripts/sync-gamedata.py (manifest, pruning, target, coverage)."""
import dataclasses
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPTS = Path(os.environ.get("GAMEDATA_SCRIPTS_DIR", Path(__file__).resolve().parents[1]))
REPOSITORY = SCRIPTS.parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


sync = load("metamod_sync_gamedata", "sync-gamedata.py")


def record(platform, module, symbol, kind, payload):
    return {"platform": platform, "module": module, "symbolName": symbol, "kind": kind,
            "payload": payload}


def function(rva):
    return {"func_rva": rva, "func_size": "0x10", "func_sig": "55 8B EC"}


def snapshot(records, game_version="test-1"):
    return {
        "schemaVersion": 5,
        "source": {"snapshotSchemaVersion": 8, "analysisOutputContractVersion": 3,
                   "gameVersion": game_version, "generator": "upstream"},
        "binaries": {
            "server": {"windows": {"crc64": "00000000000000a1", "pdb": "x"},
                       "linux": {"crc64": "00000000000000b1"}},
            "engine": {"windows": {"crc64": "00000000000000a2"},
                       "linux": {"crc64": "00000000000000b2"}},
        },
        "records": records,
    }


class ManifestTests(unittest.TestCase):
    def write_manifest(self, document):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "manifest.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    def base_manifest(self, **extra):
        document = {"schemaVersion": 1, "name": "test", "gameVersions": ["test-1"],
                    "symbols": {"server": {"Common": "function"}}}
        document.update(extra)
        return document

    def test_platforms_default_to_windows_and_linux(self):
        manifest = sync.load_manifest(self.write_manifest(self.base_manifest()))
        self.assertEqual(("windows", "linux"), manifest.platforms)
        self.assertEqual({}, manifest.platform_symbols)

    def test_platform_symbols_are_parsed_per_platform(self):
        manifest = sync.load_manifest(self.write_manifest(self.base_manifest(
            platformSymbols={"windows": {"server": {"Win(unsigned int)": "function"}},
                             "linux": {"server": {"Lin(unsigned long)": "function"}}})))
        self.assertEqual({"server": {"Win(unsigned int)": "function"}},
                         manifest.platform_symbols["windows"])
        self.assertEqual({"server": {"Lin(unsigned long)": "function"}},
                         manifest.platform_symbols["linux"])

    def test_reject_invalid_platform_declarations(self):
        invalid = (
            {"platforms": []},
            {"platforms": ["macos"]},
            {"platforms": "windows"},
            {"platforms": ["windows"],
             "platformSymbols": {"linux": {"server": {"Lin": "function"}}}},
        )
        for extra in invalid:
            with self.subTest(extra=extra):
                path = self.write_manifest(self.base_manifest(**extra))
                with self.assertRaises(sync.UpdateError):
                    sync.load_manifest(path)

    def test_reject_unsupported_kind(self):
        path = self.write_manifest(self.base_manifest(symbols={"server": {"Common": "vtable"}}))
        with self.assertRaises(sync.UpdateError):
            sync.load_manifest(path)

    def test_component_manifests_load(self):
        for relative in ("asext/scripts/manifests/asext.json",
                         "fallguys/scripts/manifests/fallguys.json"):
            path = REPOSITORY / relative
            if not path.is_file():
                continue
            with self.subTest(manifest=relative):
                manifest = sync.load_manifest(path)
                self.assertEqual(("windows", "linux"), manifest.platforms)
                self.assertEqual({"windows", "linux"}, set(manifest.platform_symbols))


class PruneTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "manifest.json"
        path.write_text(json.dumps({
            "schemaVersion": 1, "name": "test", "gameVersions": ["test-1"],
            "symbols": {"server": {"Common": "function"}, "engine": {"g_var": "global"}},
            "platformSymbols": {"windows": {"server": {"Win(unsigned int)": "function"}},
                                "linux": {"server": {"Lin(unsigned long)": "function"}}},
        }), encoding="utf-8")
        self.manifest = sync.load_manifest(path)
        self.document = snapshot([
            record("windows", "server", "Common", "function", function("0x1000")),
            record("linux", "server", "Common", "function", function("0x2000")),
            record("windows", "server", "Win(unsigned int)", "function", function("0x1100")),
            record("linux", "server", "Win(unsigned int)", "function", function("0x2100")),
            record("windows", "server", "Lin(unsigned long)", "function", function("0x1200")),
            record("linux", "server", "Lin(unsigned long)", "function", function("0x2200")),
            record("windows", "engine", "g_var", "global", {"gv_rva": "0x3000", "gv_sig": "A1"}),
            record("linux", "engine", "g_var", "global", {"gv_rva": "0x4000"}),
            record("windows", "server", "Unrequested", "function", function("0x1300")),
            record("linux", "client", "Common", "function", function("0x5000")),
        ])

    def kept(self, result):
        return {(r["platform"], r["module"], r["symbolName"]) for r in result["records"]}

    def test_both_platforms_are_retained(self):
        result = sync.prune_snapshot(self.document, self.manifest, "test-1")
        self.assertEqual({
            ("windows", "server", "Common"),
            ("linux", "server", "Common"),
            ("windows", "server", "Win(unsigned int)"),
            ("linux", "server", "Lin(unsigned long)"),
            ("windows", "engine", "g_var"),
            ("linux", "engine", "g_var"),
        }, self.kept(result))

    def test_payloads_keep_only_consumed_fields(self):
        result = sync.prune_snapshot(self.document, self.manifest, "test-1")
        payloads = {(r["platform"], r["symbolName"]): r["payload"] for r in result["records"]}
        self.assertEqual({"func_rva": "0x2000", "func_size": "0x10"}, payloads[("linux", "Common")])
        self.assertEqual({"gv_rva": "0x3000"}, payloads[("windows", "g_var")])

    def test_binaries_keep_crc64_of_used_modules(self):
        result = sync.prune_snapshot(self.document, self.manifest, "test-1")
        self.assertEqual({
            "server": {"windows": {"crc64": "00000000000000a1"},
                       "linux": {"crc64": "00000000000000b1"}},
            "engine": {"windows": {"crc64": "00000000000000a2"},
                       "linux": {"crc64": "00000000000000b2"}},
        }, result["binaries"])
        self.assertEqual({"snapshotSchemaVersion": 8, "analysisOutputContractVersion": 3,
                          "gameVersion": "test-1"}, result["source"])

    def test_platform_filter_drops_undeclared_platforms(self):
        windows_only = dataclasses.replace(
            self.manifest, platforms=("windows",),
            platform_symbols={"windows": self.manifest.platform_symbols["windows"]})
        result = sync.prune_snapshot(self.document, windows_only, "test-1")
        self.assertEqual({"windows"}, {r["platform"] for r in result["records"]})
        self.assertEqual({"windows"}, set(result["binaries"]["server"]))


class TargetPathTests(unittest.TestCase):
    def test_accept_metamod_catalog_locations(self):
        with tempfile.TemporaryDirectory() as root:
            for relative in ("addons/metamod/gamedata", "addons/metamod/gamedata/asext",
                             "ADDONS/Metamod/GameData/fallguys"):
                with self.subTest(relative=relative):
                    target = Path(root) / relative
                    self.assertEqual(Path(os.path.abspath(str(target))),
                                     sync.validate_target_path(target))

    def test_reject_other_locations(self):
        with tempfile.TemporaryDirectory() as root:
            for relative in ("gamedata", "addons/gamedata", "metamod/gamedata/asext",
                             "addons/metamod/gamedata/asext/nested", "addons/metamod/dlls"):
                with self.subTest(relative=relative):
                    with self.assertRaises(sync.UpdateError):
                        sync.validate_target_path(Path(root) / relative)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.target = Path(directory.name) / "addons" / "metamod" / "gamedata" / "test"
        self.target.mkdir(parents=True)
        manifest_path = Path(directory.name) / "manifest.json"
        manifest_path.write_text(json.dumps({
            "schemaVersion": 1, "name": "test", "gameVersions": ["test-1"],
            "symbols": {"server": {"Common": "function"}},
            "platformSymbols": {"windows": {"server": {"Win": "function"}},
                                "linux": {"server": {"Lin": "function"}}},
        }), encoding="utf-8")
        self.manifest = sync.load_manifest(manifest_path)

    def publish(self, records):
        contents = sync.serialize_pruned(snapshot(records))
        (self.target / "test-1.json").write_bytes(contents)
        index = {"schemaVersion": 4, "versions": [{
            "gameVersion": "test-1", "url": "test-1.json",
            "sha256": hashlib.sha256(contents).hexdigest(), "size": len(contents),
            "snapshotSchemaVersion": 8, "fileCount": 1,
            "lastPublishTime": "2026-01-01T00:00:00Z"}]}
        (self.target / "index.json").write_text(json.dumps(index), encoding="utf-8")

    def complete_records(self):
        return [
            record("windows", "server", "Common", "function", function("0x1000")),
            record("linux", "server", "Common", "function", function("0x2000")),
            record("windows", "server", "Win", "function", function("0x1100")),
            record("linux", "server", "Lin", "function", function("0x2100")),
        ]

    def test_complete_target_passes(self):
        self.publish(self.complete_records())
        sync.validate_manifest_coverage(self.target, self.manifest)

    def test_missing_platform_record_is_reported(self):
        records = [r for r in self.complete_records()
                   if not (r["platform"] == "linux" and r["symbolName"] == "Lin")]
        self.publish(records)
        with self.assertRaisesRegex(sync.UpdateError, r"test-1 \(linux\).*Lin"):
            sync.validate_manifest_coverage(self.target, self.manifest)

    def test_record_of_the_other_platform_does_not_satisfy_coverage(self):
        records = [r for r in self.complete_records()
                   if not (r["platform"] == "linux" and r["symbolName"] == "Common")]
        self.publish(records)
        with self.assertRaisesRegex(sync.UpdateError, r"test-1 \(linux\).*Common"):
            sync.validate_manifest_coverage(self.target, self.manifest)


if __name__ == "__main__":
    unittest.main()
