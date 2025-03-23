#!/usr/bin/env python3

import os
import shutil
import subprocess

TESTSUITE_PATH = "testsuite"
OUTPUT_PATH = "tests"
WAST2JSON_PATH = "wabt/bin/wast2json"
SPECTEST_PATH = "spectest.wasm"

ENABLED_PROPOSALS = [
    # [ "annotations", "--enable-annotations" ],
    # [ "exception-handling", "--enable-exceptions" ],
    # [ "extended-const", "--enable-extended-const" ],
    # [ "function-references", "--enable-function-references" ],
    # [ "gc", "--enable-gc" ],
    # [ "memory64", "--enable-memory64" ],
    # [ "multi-memory", "--enable-multi-memory" ],
    # [ "relaxed-simd", "--enable-relaxed-simd" ],
    # [ "tail-call", "--enable-tail-call" ],
    # [ "threads", "--enable-threads" ],
]

ENABLE_SIMD = True

shutil.rmtree(OUTPUT_PATH)

for file in os.listdir(TESTSUITE_PATH):
    if not file.endswith(".wast"):
        continue

    if not ENABLE_SIMD and file.startswith("simd_"):
        continue

    test_name = file.removesuffix(".wast")
    test_directory = os.path.join(OUTPUT_PATH, test_name)
    wast_directory = os.path.join(TESTSUITE_PATH, file)

    os.makedirs(test_directory)
    subprocess.run([WAST2JSON_PATH, wast_directory, f"--output={os.path.join(test_directory, f'{test_name}.json')}"])

for proposal, flag in ENABLED_PROPOSALS:
    proposal_path = os.path.join(TESTSUITE_PATH, "proposals", proposal)
    for file in os.listdir(proposal_path):
        if not file.endswith(".wast"):
            continue

        test_name = file.removesuffix(".wast")
        test_directory = os.path.join(OUTPUT_PATH, "proposals", proposal, test_name)
        wast_directory = os.path.join(TESTSUITE_PATH, "proposals", proposal, file)

        os.makedirs(test_directory)
        subprocess.run([WAST2JSON_PATH, wast_directory, flag, f"--output={os.path.join(test_directory, f'{test_name}.json')}"])

shutil.copyfile(SPECTEST_PATH, os.path.join(OUTPUT_PATH, SPECTEST_PATH))
