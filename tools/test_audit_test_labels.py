#!/usr/bin/env python3
"""Regression tests for the capability launcher, including CTest result policies."""

import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).with_name("audit_test_labels.py")
spec = importlib.util.spec_from_file_location("audit_test_labels", SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditTestLabels(unittest.TestCase):
    def test_capabilities(self):
        for label, markers in audit.MARKERS.items():
            for marker in markers:
                with self.subTest(label=label, marker=marker):
                    self.assertEqual(audit.missing_capabilities(set(), marker), [label])
                    self.assertEqual(audit.missing_capabilities({label}, marker), [])
        self.assertEqual(audit.missing_capabilities(set(), "Success!\n"), [])

    def test_multithreaded_execution(self):
        parallel = audit.PARALLEL_MARKER + "y\n"
        jit = audit.JIT_MARKER + "\n"
        for output in ("", parallel, jit, jit + parallel):
            with self.subTest(output=output):
                self.assertEqual(audit.missing_capabilities(set(), output), [])
        for output in (parallel + jit, jit + parallel + jit):
            with self.subTest(output=output):
                self.assertEqual(
                    audit.missing_capabilities(set(), output), ["multithreaded"]
                )
                self.assertEqual(
                    audit.missing_capabilities({"multithreaded"}, output), []
                )

    def test_gpu_target_queries(self):
        for api in ("cuda", "vulkan"):
            query = audit.GPU_TARGET_QUERY_MARKER + api
            use = "Using device interface: " + api
            for newline in ("\n", "\r\n"):
                with self.subTest(api=api, newline=newline):
                    probe = query + newline + use + newline
                    self.assertEqual(audit.missing_capabilities(set(), probe), [])
                    for output in (
                        use + newline + probe,
                        probe + use,
                        query + newline + "intervening output" + newline + use,
                        query + newline + "Using device interface: metal",
                        query + newline + "Compiling GPU kernel: kernel",
                        probe + "Compiling GPU kernel: kernel",
                    ):
                        with self.subTest(output=output):
                            self.assertEqual(
                                audit.missing_capabilities(set(), output), ["gpu"]
                            )
                            self.assertEqual(
                                audit.missing_capabilities({"gpu"}, output), []
                            )

    def test_ctest(self):
        with tempfile.TemporaryDirectory(prefix="halide-label-audit-") as tmp:
            root = pathlib.Path(tmp)
            tmp = root.as_posix()
            python = pathlib.Path(sys.executable).as_posix()
            child = root / "child.py"
            child.write_text(
                "import os, sys\n"
                "print('target=' + os.environ['HL_JIT_TARGET'])\n"
                "print('cwd=' + os.getcwd())\n"
                "if sys.argv[1] != 'plain':\n"
                "    print('Reading target from environment: HL_JIT_TARGET')\n"
                "if sys.argv[1] == 'jit_then_schedule':\n"
                f"    print({audit.JIT_MARKER!r})\n"
                f"    print({audit.PARALLEL_MARKER!r})\n"
                "elif sys.argv[1].startswith('schedule'):\n"
                f"    print({audit.PARALLEL_MARKER!r})\n"
                "    if 'then_jit' in sys.argv[1]:\n"
                f"        print({audit.JIT_MARKER!r})\n"
                "elif sys.argv[1] == 'jit_only':\n"
                f"    print({audit.JIT_MARKER!r})\n"
                "if sys.argv[1].startswith('gpu_probe'):\n"
                f"    print({(audit.GPU_TARGET_QUERY_MARKER + 'cuda')!r})\n"
                "    print('Using device interface: cuda')\n"
                "    if sys.argv[1] == 'gpu_probe_then_use':\n"
                "        print('Using device interface: cuda')\n"
                "if sys.argv[1].startswith('unicode'):\n"
                "    sys.stdout.flush()\n"
                "    sys.stdout.buffer.write(b'\\xe2\\x94\\x82\\n')\n"
                "    sys.stdout.buffer.flush()\n"
                "print('Success!')\n"
                "if 'skip' in sys.argv[1]: print('[SKIP]')\n"
                "sys.exit(1 if 'expected' in sys.argv[1] and 'success' not in sys.argv[1] else 0)\n"
            )
            (root / "tools").mkdir()
            (root / "tools" / SCRIPT.name).write_text(SCRIPT.read_text())
            helper = SCRIPT.parent.parent / "cmake" / "HalideTestHelpers.cmake"
            root_cmake = [
                "cmake_minimum_required(VERSION 3.29)",
                "project(Halide NONE)",
                "enable_testing()",
                f'list(APPEND CMAKE_MODULE_PATH "{helper.parent.as_posix()}")',
                'option(Halide_BUILDING_IN_CI "" ON)',
                'option(WITH_TESTS "" ON)',
                f'set(Python3_EXECUTABLE "{python}")',
                # The result-policy fixture needs no compiled helper libraries.
                "foreach(helper Test ExpectAbort TerminateHandler)",
                "    add_library(Halide::${helper} INTERFACE IMPORTED GLOBAL)",
                "endforeach()",
                "function(set_halide_compiler_warnings target)",
                '    if(NOT TARGET "${target}")',
                '        message(FATAL_ERROR "Compiler warnings require an executable target")',
                "    endif()",
                "endfunction()",
                "if(WITH_TESTS)",
                "    add_subdirectory(test)",
                "endif()",
                "add_subdirectory(tutorial)",
            ]
            # An imported executable exercises TEST_LAUNCHER without a C++ build.
            cmake = [
                f'include("{helper.as_posix()}")',
                f'include("{helper.as_posix()}")',
            ]
            cases = {
                "plain": (False, False, False),
                "unicode": (True, False, False),
                "unicode_violation": (False, False, False),
                "gpu_probe": (True, False, False),
                "gpu_probe_then_use": (True, False, False),
                "allowed": (True, False, False),
                "violation": (False, False, False),
                "skip_violation": (False, False, True),
                "expected_violation": (False, True, False),
                "expected_allowed": (True, True, False),
                "expected_success": (True, True, False),
                "skip_allowed": (True, False, True),
                "schedule_only": (True, False, False),
                "jit_only": (True, False, False),
                "jit_then_schedule": (True, False, False),
                "schedule_then_jit": (True, False, False),
                "schedule_then_jit_permitted": (True, False, False),
            }
            for name, (allowed, expected, skip) in cases.items():
                options = "EXPECT_FAILURE USE_EXIT_CODE_ONLY" if expected else ""
                target = name if name == "plain" else f"child_{name}"
                test_name = "" if target == name else f"NAME {name}"
                cmake += [
                    f"add_executable({target} IMPORTED)",
                    f'set_property(TARGET {target} PROPERTY IMPORTED_LOCATION "{python}")',
                    f'add_halide_test({target} {test_name} ARGS "{child.as_posix()}" {name} '
                    f'WORKING_DIRECTORY "{tmp}" GROUPS fixture {options})',
                    f"set_tests_properties({name} PROPERTIES "
                    'ENVIRONMENT "HL_JIT_TARGET=unchanged")',
                ]
                if allowed:
                    cmake += [
                        f"set_tests_properties({name} PROPERTIES LABELS target_from_environment)"
                    ]
                if name.startswith("unicode"):
                    cmake += [
                        f"set_property(TEST {name} APPEND PROPERTY ENVIRONMENT "
                        '"PYTHONIOENCODING=cp1252")'
                    ]
                if name == "schedule_then_jit_permitted":
                    cmake += [
                        f"set_property(TEST {name} APPEND PROPERTY LABELS multithreaded)"
                    ]
                if skip:
                    cmake += [
                        f'set_tests_properties({name} PROPERTIES SKIP_REGULAR_EXPRESSION "\\\\[SKIP\\\\]")'
                    ]
            (root / "CMakeLists.txt").write_text("\n".join(root_cmake))
            (root / "test").mkdir()
            (root / "test" / "CMakeLists.txt").write_text("\n".join(cmake))
            (root / "tutorial").mkdir()
            (root / "tutorial" / "CMakeLists.txt").write_text(
                f'include("{helper.as_posix()}")\n'
                "add_executable(tutorial_child IMPORTED)\n"
                f'set_property(TARGET tutorial_child PROPERTY IMPORTED_LOCATION "{python}")\n'
                f"add_halide_test(tutorial_child NAME tutorial_plain USE_EXIT_CODE_ONLY "
                f'ARGS "{child.as_posix()}" plain tutorial WORKING_DIRECTORY "{tmp}" GROUPS tutorial)\n'
                "set_tests_properties(tutorial_plain PROPERTIES "
                'ENVIRONMENT "HL_JIT_TARGET=unchanged")\n'
            )
            subprocess.run(
                ["cmake", "-S", tmp, "-B", tmp], check=True, capture_output=True
            )
            result = subprocess.run(
                [
                    "ctest",
                    "--test-dir",
                    tmp,
                    "-C",
                    "Debug",
                    "--output-on-failure",
                    "--output-junit",
                    str(root / "results.xml"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            import xml.etree.ElementTree as ET

            results = {
                t.attrib["name"]: t
                for t in ET.parse(root / "results.xml").iter("testcase")
            }
            for name, test in results.items():
                self.assertTrue(
                    test.attrib.get("status") != "notrun" or name == "skip_allowed",
                    f"{name} did not run:\n{result.stdout}{result.stderr}",
                )
            for name in (
                "violation",
                "skip_violation",
                "expected_violation",
                "unicode_violation",
            ):
                self.assertIsNotNone(results[name].find("failure"), result.stdout)
                output = results[name].findtext("system-out")
                self.assertIn(audit.FAILURE_MARKER, output)
                if name != "expected_violation":
                    self.assertIn("target=unchanged", output)
                    self.assertIn(
                        "cwd=" + root.resolve().as_posix(), output.replace("\\", "/")
                    )
            for name in (
                "plain",
                "allowed",
                "expected_allowed",
                "tutorial_plain",
                "unicode",
                "gpu_probe",
            ):
                self.assertIsNone(results[name].find("failure"), result.stdout)
            for name in ("unicode", "unicode_violation"):
                self.assertIn("\u2502", results[name].findtext("system-out"))
            self.assertIsNotNone(
                results["gpu_probe_then_use"].find("failure"), result.stdout
            )
            self.assertIn(
                "requires labels: gpu",
                results["gpu_probe_then_use"].findtext("system-out"),
            )
            for name in (
                "schedule_only",
                "jit_only",
                "jit_then_schedule",
                "schedule_then_jit_permitted",
            ):
                self.assertIsNone(results[name].find("failure"), result.stdout)
            self.assertIsNotNone(
                results["schedule_then_jit"].find("failure"), result.stdout
            )
            self.assertIn(
                "requires labels: multithreaded",
                results["schedule_then_jit"].findtext("system-out"),
            )
            self.assertIsNotNone(
                results["expected_success"].find("failure"), result.stdout
            )
            self.assertIsNotNone(results["skip_allowed"].find("skipped"), result.stdout)

            for ci, with_tests in (("ON", "OFF"), ("OFF", "ON")):
                subprocess.run(
                    [
                        "cmake",
                        "-S",
                        tmp,
                        "-B",
                        tmp,
                        f"-DHalide_BUILDING_IN_CI={ci}",
                        f"-DWITH_TESTS={with_tests}",
                    ],
                    check=True,
                    capture_output=True,
                )
                tests = json.loads(
                    subprocess.check_output(
                        [
                            "ctest",
                            "--test-dir",
                            tmp,
                            "-C",
                            "Debug",
                            "--show-only=json-v1",
                        ]
                    )
                )["tests"]
                for test in tests:
                    command = " ".join(test["command"])
                    self.assertEqual(SCRIPT.name in command, ci == "ON")
                    expressions = next(
                        (
                            p["value"]
                            for p in test["properties"]
                            if p["name"] == "FAIL_REGULAR_EXPRESSION"
                        ),
                        [],
                    )
                    self.assertEqual(
                        expressions.count(audit.FAILURE_MARKER), int(ci == "ON")
                    )


if __name__ == "__main__":
    unittest.main()
