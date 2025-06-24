#!/usr/bin/env python3

import os
import subprocess
import platform
import shutil

TEST_DATA_PATH = "test_data"

TESTSUITE_SOURCE_PATH = os.path.join(TEST_DATA_PATH, "testsuite")
TESTSUITE_PROCESSED_PATH = os.path.join(TEST_DATA_PATH, "testsuite-processed")

WABT_VERSION = "1.0.36"
WABT_URL_X86_64 = f"https://github.com/WebAssembly/wabt/releases/download/{WABT_VERSION}/wabt-{WABT_VERSION}-ubuntu-20.04.tar.gz"
WABT_GIT_REPO = "https://github.com/WebAssembly/wabt"
WABT_DOWNLOADED_FILE = os.path.join(TEST_DATA_PATH, f"wabt-{WABT_VERSION}-ubuntu-20.04.tar.gz")
WABT_DOWNLOADED_DIR = os.path.join(TEST_DATA_PATH, f"wabt-{WABT_VERSION}")

WABT_PATH = os.path.join(TEST_DATA_PATH, "wabt")
WAST2JSON_PATH = os.path.join(WABT_PATH, "bin", "wast2json")

ENABLED_PROPOSALS: dict[str, str] = [
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
] # type: ignore

ENABLE_SIMD = True

def check_executable(exe: str):
    try:
        subprocess.run([exe, "--version"], check=True, capture_output=True)
    except subprocess.CalledProcessError:
        print(f"{exe} is not installed")
        exit(1)

check_executable("wget")
check_executable("tar")
check_executable("git")
check_executable("make")

os.makedirs(TEST_DATA_PATH, exist_ok=True)

if not os.path.exists(WABT_PATH):
    if platform.processor() == "x86_64":
        subprocess.run(["wget", "-P", TEST_DATA_PATH, WABT_URL_X86_64])
        subprocess.run(["tar", "-xvf", WABT_DOWNLOADED_FILE, "-C", TEST_DATA_PATH])
        subprocess.run(["mv", WABT_DOWNLOADED_DIR, WABT_PATH])
    else:
        subprocess.run(["git", "clone", WABT_GIT_REPO, WABT_PATH, "--depth=1", "--recursive"])
        subprocess.run(["make", "-C", WABT_PATH, f"-j{os.cpu_count()}"])

if not os.path.exists(TESTSUITE_SOURCE_PATH):
    subprocess.run(["git", "clone", "https://github.com/WebAssembly/testsuite", TESTSUITE_SOURCE_PATH, "--depth=1"])

shutil.rmtree(TESTSUITE_PROCESSED_PATH, ignore_errors=True)

for file in os.listdir(TESTSUITE_SOURCE_PATH):
    if not file.endswith(".wast"):
        continue

    if not ENABLE_SIMD and file.startswith("simd_"):
        continue

    test_name = file.removesuffix(".wast")
    test_directory = os.path.join(TESTSUITE_PROCESSED_PATH, test_name)
    wast_directory = os.path.join(TESTSUITE_SOURCE_PATH, file)

    os.makedirs(test_directory)
    subprocess.run([WAST2JSON_PATH, wast_directory, f"--output={os.path.join(test_directory, f'{test_name}.json')}"])

for proposal, flag in ENABLED_PROPOSALS:
    proposal_path = os.path.join(TESTSUITE_SOURCE_PATH, "proposals", proposal)
    for file in os.listdir(proposal_path):
        if not file.endswith(".wast"):
            continue

        test_name = file.removesuffix(".wast")
        test_directory = os.path.join(TESTSUITE_PROCESSED_PATH, "proposals", proposal, test_name)
        wast_directory = os.path.join(TESTSUITE_SOURCE_PATH, "proposals", proposal, file)

        os.makedirs(test_directory)
        subprocess.run([WAST2JSON_PATH, wast_directory, flag, f"--output={os.path.join(test_directory, f'{test_name}.json')}"])
