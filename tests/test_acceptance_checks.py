#!/usr/bin/env python3
"""Prevent zero-test, skipped-kernel-test and dirty-exit acceptance false positives."""
import importlib.util
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

MODULE = Path(__file__).resolve().parents[1] / "scripts/check_sp3_acceptance.py"
SPEC = importlib.util.spec_from_file_location("acceptance", MODULE)
acceptance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(acceptance)


def suite():
    root = ET.Element("testsuite")
    for name in sorted(acceptance.REQUIRED_TESTS):
        ET.SubElement(root, "testcase", name=name, status="run")
    return root


class AcceptanceChecks(unittest.TestCase):
    def test_six_real_passes(self):
        self.assertEqual(acceptance.validate_ctest(suite())["passed"], 6)

    def test_no_tests_is_failure(self):
        with self.assertRaises(ValueError):
            acceptance.validate_ctest(ET.Element("testsuite"))

    def test_missing_kernel_test_is_failure(self):
        root = suite()
        root.remove(root.find("testcase[@name='integration_fd']"))
        with self.assertRaises(ValueError):
            acceptance.validate_ctest(root)

    def test_skip_failure_error_or_notrun_is_failure(self):
        for tag in ("skipped", "failure", "error", "notrun"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                root = suite()
                case = root.find("testcase[@name='integration_fd']")
                if tag == "notrun":
                    case.set("status", "notrun")
                else:
                    ET.SubElement(case, tag)
                acceptance.validate_ctest(root)

    def test_clean_exit(self):
        snapshot = {"state": "disabled", "pins": 0, "loader_pids": [], "executor_pids": []}
        self.assertEqual(acceptance.validate_idle(snapshot), snapshot)

    def test_dirty_exit_is_failure(self):
        for key, value in (("state", "enabled"), ("pins", 1), ("loader_pids", [42]), ("executor_pids", [42])):
            with self.subTest(key=key), self.assertRaises(ValueError):
                snapshot = {"state": "disabled", "pins": 0, "loader_pids": [], "executor_pids": []}
                snapshot[key] = value
                acceptance.validate_idle(snapshot)


if __name__ == "__main__":
    unittest.main()
