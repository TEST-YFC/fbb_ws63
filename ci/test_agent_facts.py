#!/usr/bin/env python3
# encoding=utf-8
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026.
# All rights reserved.
"""Validate the existing WS63 facts consumed through hs-fbb-cli."""

import json
import unittest
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = ROOT / "src"
CHIP_CONFIG = (
    SDK_ROOT / "build" / "config" / "target_config" / "ws63" / "ws63.json"
)
EXPECTED_EXAMPLE_ROOTS = {
    "application/samples/peripheral",
    "application/samples/bt/ble",
    "application/samples/bt/sle",
    "application/samples/wifi",
    "application/samples/radar",
    "application/samples/os/memory",
    "application/samples/os/interrupt",
    "application/samples/os/task",
}
REQUIRED_EXAMPLES = {
    "application/samples/peripheral/adc",
    "application/samples/peripheral/blinky",
    "application/samples/peripheral/dma",
    "application/samples/peripheral/helloworld",
    "application/samples/peripheral/helloworld_oled",
    "application/samples/peripheral/i2c",
    "application/samples/peripheral/i2s_dma_lli",
    "application/samples/peripheral/pinctrl",
    "application/samples/peripheral/pwm",
    "application/samples/peripheral/sfc",
    "application/samples/peripheral/sfc_perf",
    "application/samples/peripheral/spi",
    "application/samples/peripheral/systick",
    "application/samples/peripheral/tcxo",
    "application/samples/peripheral/timer",
    "application/samples/peripheral/uart",
    "application/samples/peripheral/watchdog",
    "application/samples/bt/ble/ble_speed_client",
    "application/samples/bt/ble/ble_speed_server",
    "application/samples/bt/ble/ble_wifi_cfg_server",
    "application/samples/bt/sle/sle_speed_client",
    "application/samples/bt/sle/sle_speed_server",
    "application/samples/wifi/alilink_sample",
    "application/samples/wifi/ble_wifi_cfg_sample",
    "application/samples/wifi/hilink_indie_upgrade",
    "application/samples/wifi/mqtt_sample",
    "application/samples/wifi/ohos_connect",
    "application/samples/wifi/softap_sample",
    "application/samples/wifi/sta_sample",
    "application/samples/radar/sta_sample",
    "application/samples/os/task/osal_task_concurrency",
}


def _load_config():
    return json.loads(CHIP_CONFIG.read_text(encoding="utf-8-sig"))


def _discover_examples(roots):
    examples = set()
    for root in roots:
        root_path = SDK_ROOT / root
        for child in root_path.iterdir():
            if child.is_dir() and (child / "CMakeLists.txt").is_file():
                examples.add(child.relative_to(SDK_ROOT).as_posix())
    return examples


class TestWs63AgentFacts(unittest.TestCase):
    def test_existing_chip_config_is_the_only_added_fact_source(self):
        self.assertTrue(CHIP_CONFIG.is_file())
        self.assertFalse((SDK_ROOT / "metadata").exists())

    def test_workflow_facts_remain_in_existing_chip_config(self):
        config = _load_config()

        self.assertEqual(config["chipName"], "ws63")
        self.assertEqual(config["compile"]["custom_build_command"], "ws63-liteos-app")
        self.assertEqual(
            config["compile"]["map_path"],
            "./output/ws63/acore/ws63-liteos-app/ws63-liteos-app.elf",
        )
        self.assertEqual(
            config["upload"]["bin_path"],
            "./output/ws63/fwpkg/ws63-liteos-app/"
            "ws63-liteos-app_all.fwpkg",
        )
        self.assertEqual(config["monitor"]["default_baud"], 115200)
        self.assertEqual(config["monitor"]["reset_method"], "at_rst")
        self.assertIn("device_module_init:: succ!",
                      config["monitor"]["boot_marker_examples"])
        self.assertEqual(config["flash"]["chiptype"], "WS63")
        self.assertEqual(config["flash"]["signalbaud"], 921600)
        self.assertEqual(config["flash"]["expected_sections"], 7)

    def test_example_roots_are_complete_safe_and_clonable(self):
        roots = _load_config()["examples"]["roots"]
        self.assertEqual(set(roots), EXPECTED_EXAMPLE_ROOTS)

        for root in roots:
            path = PurePosixPath(root)
            self.assertFalse(path.is_absolute())
            self.assertNotIn("..", path.parts)
            resolved = SDK_ROOT / path
            self.assertTrue(resolved.is_dir(), root)
            self.assertTrue(
                any(
                    child.is_dir() and (child / "CMakeLists.txt").is_file()
                    for child in resolved.iterdir()
                ),
                f"{root} has no directly clonable example",
            )

        self.assertTrue(REQUIRED_EXAMPLES.issubset(_discover_examples(roots)))

    def test_kconfig_and_runtime_evidence_remain_in_sdk_source(self):
        bt_kconfig = (
            SDK_ROOT / "application" / "samples" / "bt" / "Kconfig"
        ).read_text(encoding="utf-8")
        sle_kconfig = (
            SDK_ROOT / "application" / "samples" / "bt" / "sle" / "Kconfig"
        ).read_text(encoding="utf-8")
        self.assertIn("SAMPLE_SUPPORT_BLE_SAMPLE", bt_kconfig)
        self.assertIn("SAMPLE_SUPPORT_SLE_SAMPLE", bt_kconfig)
        self.assertIn("SAMPLE_SUPPORT_CHBA_SAMPLE", bt_kconfig)
        self.assertIn("SAMPLE_SUPPORT_SLE_SPEED_SERVER_SAMPLE", sle_kconfig)
        self.assertIn("SAMPLE_SUPPORT_SLE_SPEED_CLIENT_SAMPLE", sle_kconfig)

        evidence = {
            "application/samples/peripheral/blinky/blinky_demo.c":
                "Blinky working.",
            "application/samples/bt/sle/sle_speed_server/src/"
            "sle_speed_server.c": "sle enable end.",
            "application/samples/wifi/sta_sample/sta_sample.c":
                "Connect succ!.",
        }
        for relative_path, marker in evidence.items():
            source = (SDK_ROOT / relative_path).read_text(encoding="utf-8")
            self.assertIn(marker, source, relative_path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
