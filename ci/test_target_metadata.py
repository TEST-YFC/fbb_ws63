#!/usr/bin/env python3
"""Validate the SDK-owned target metadata against the WS63 source tree."""

import ast
import json
from pathlib import Path, PurePosixPath
import unittest
import xml.etree.ElementTree as ET


REPO_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = REPO_ROOT / "src"
METADATA_ROOT = SDK_ROOT / "metadata"


def load_json(relative_path):
    path = METADATA_ROOT / relative_path
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def source_target_ids():
    config_path = (
        SDK_ROOT / "build" / "config" / "target_config" / "ws63" / "config.py"
    )
    tree = ast.parse(config_path.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            if any(isinstance(target, ast.Name) and target.id == "target"
                   for target in node.targets):
                value = ast.literal_eval(node.value)
                return set(value)
    raise AssertionError("target dictionary was not found in WS63 config.py")


def source_sample_paths():
    samples_root = SDK_ROOT / "application" / "samples"
    paths = set()
    for cmake in samples_root.rglob("CMakeLists.txt"):
        sample_dir = cmake.parent
        relative_parts = sample_dir.relative_to(samples_root).parts
        if (
            sample_dir == samples_root
            or "src" in relative_parts
            or "address_mapping" in relative_parts
        ):
            continue
        nested = [
            path for path in sample_dir.rglob("CMakeLists.txt")
            if path != cmake
            and "src" not in path.relative_to(sample_dir).parts
            and "address_mapping" not in path.relative_to(sample_dir).parts
        ]
        if nested:
            continue
        paths.add(sample_dir.relative_to(SDK_ROOT).as_posix())
    return paths


def assert_repo_relative(test_case, reference, *, must_exist=True):
    test_case.assertIsInstance(reference, str)
    posix = PurePosixPath(reference)
    test_case.assertFalse(posix.is_absolute())
    test_case.assertNotIn("..", posix.parts)
    test_case.assertEqual(reference, posix.as_posix())
    if must_exist:
        test_case.assertTrue((SDK_ROOT / reference).is_file(), reference)


class TestTargetMetadata(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sdk = load_json("sdk.json")
        cls.device = load_json("devices/ws63.json")
        cls.samples = load_json("samples/ws63.json")
        cls.known_issues = load_json("known-issues/ws63.json")

    def test_sdk_profile_declares_ws63_and_target_provider_version(self):
        self.assertEqual(self.sdk["schema_version"], "1.0")
        self.assertEqual(self.sdk["id"], "fbb_ws63")
        self.assertEqual(self.sdk["framework"], "fbb")
        self.assertEqual(self.sdk["default_device"], "ws63")
        self.assertEqual(self.sdk["devices"], ["ws63"])
        self.assertEqual(self.sdk["requires"]["hs_fbb_cli"], ">=1.1.0")

    def test_device_profile_matches_build_target_source(self):
        self.assertEqual(self.device["schema_version"], "1.0")
        self.assertEqual(self.device["id"], "ws63")
        profile_targets = {
            target["id"] for target in self.device["build"]["targets"]
        }
        self.assertEqual(profile_targets, source_target_ids())
        self.assertIn(
            self.device["build"]["default_target"],
            profile_targets,
        )

    def test_device_profile_architecture_matches_compiler_flags(self):
        architecture = self.device["architecture"]
        self.assertEqual(architecture["isa"], "riscv")
        self.assertEqual(architecture["xlen"], 32)
        self.assertEqual(architecture["extensions"], ["i", "m", "f", "c"])
        self.assertEqual(architecture["abi"], "ilp32f")
        target_config = (
            SDK_ROOT / "build" / "config" / "target_config" / "ws63"
            / "target_config.py"
        ).read_text(encoding="utf-8")
        self.assertIn("-march=rv32imfc", target_config)
        self.assertIn("-mabi=ilp32f", target_config)

    def test_profile_matches_overlapping_legacy_fields(self):
        legacy_path = (
            SDK_ROOT / "build" / "config" / "target_config" / "ws63"
            / "ws63.json"
        )
        legacy = json.loads(legacy_path.read_text(encoding="utf-8"))
        required_cli = self.sdk["requires"]["hs_fbb_cli"].removeprefix(">=")
        self.assertEqual(
            legacy["requires"]["min_cli_version"],
            required_cli,
        )
        self.assertEqual(
            legacy["compile"]["custom_build_command"],
            self.device["build"]["default_target"],
        )
        self.assertEqual(
            legacy["monitor"]["default_baud"],
            self.device["capabilities"]["monitor"]["default_baud"],
        )
        self.assertEqual(
            legacy["upload"]["protocol"],
            self.device["capabilities"]["flash"]["protocols"],
        )
        self.assertEqual(
            legacy["debug"]["client"],
            self.device["capabilities"]["debug"]["clients"],
        )

    def test_artifact_references_are_repository_relative_and_present(self):
        for reference in self.device["artifacts"].values():
            assert_repo_relative(self, reference)

    def test_sample_index_matches_leaf_samples_in_source(self):
        self.assertEqual(self.samples["schema_version"], "1.0")
        self.assertEqual(self.samples["device"], "ws63")
        indexed_paths = {entry["path"] for entry in self.samples["samples"]}
        self.assertEqual(indexed_paths, source_sample_paths())
        self.assertEqual(
            len(indexed_paths),
            len(self.samples["samples"]),
            "sample paths must be unique",
        )
        target_ids = source_target_ids()
        for entry in self.samples["samples"]:
            self.assertIn(entry["build_target"], target_ids)
            self.assertEqual(entry["id"], entry["path"].removeprefix(
                "application/samples/"
            ))
            self.assertTrue((SDK_ROOT / entry["path"] / "CMakeLists.txt").is_file())

    def test_known_issue_sources_are_traceable(self):
        self.assertEqual(self.known_issues["schema_version"], "1.0")
        self.assertEqual(self.known_issues["device"], "ws63")
        issue_ids = [issue["id"] for issue in self.known_issues["issues"]]
        self.assertEqual(len(issue_ids), len(set(issue_ids)))
        for issue in self.known_issues["issues"]:
            self.assertTrue(issue["summary"])
            self.assertTrue(issue["sources"])
            for reference in issue["sources"]:
                assert_repo_relative(self, reference)

    def test_svd_has_cmsis_svd_device_and_register_structure(self):
        svd_path = SDK_ROOT / self.device["artifacts"]["svd"]
        root = ET.parse(svd_path).getroot()
        self.assertEqual(root.tag, "device")
        self.assertEqual(root.attrib["schemaVersion"], "1.3")
        self.assertEqual(root.findtext("name"), "WS63")
        self.assertEqual(root.findtext("addressUnitBits"), "8")
        self.assertEqual(root.findtext("width"), "32")
        peripherals = root.findall("./peripherals/peripheral")
        self.assertGreater(len(peripherals), 0)
        for peripheral in peripherals:
            self.assertIsNotNone(peripheral.findtext("name"))
            self.assertIsNotNone(peripheral.findtext("baseAddress"))
            registers = peripheral.findall("./registers/register")
            offsets = [register.findtext("addressOffset") for register in registers]
            self.assertEqual(
                len(offsets),
                len(set(offsets)),
                f"{peripheral.findtext('name')} contains duplicate register offsets",
            )
            for register in registers:
                self.assertIsNotNone(register.findtext("name"))
                self.assertIsNotNone(register.findtext("addressOffset"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
