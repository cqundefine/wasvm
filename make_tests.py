#!/usr/bin/env python3

import os
import subprocess
import platform
import shutil

TEST_DATA_PATH = "test_data"

TESTSUITE_SOURCE_PATH = os.path.join(TEST_DATA_PATH, "testsuite")
TESTSUITE_PROCESSED_PATH = os.path.join(TEST_DATA_PATH, "testsuite-processed")

TESTSUITE_PINNED = True
TESTSUITE_COMMIT = "a8101597d3c3c660086c3cd1eedee608ff18d3c3"

WASM_TOOLS_VERSION = "1.239.0"

WASM_TOOLS_URL_X86_64 = f"https://github.com/bytecodealliance/wasm-tools/releases/download/v{WASM_TOOLS_VERSION}/wasm-tools-{WASM_TOOLS_VERSION}-x86_64-linux.tar.gz"
WASM_TOOLS_DOWNLOADED_FILE_X86_64 = os.path.join(TEST_DATA_PATH, f"wasm-tools-{WASM_TOOLS_VERSION}-x86_64-linux.tar.gz")
WASM_TOOLS_DOWNLOADED_DIR_X86_64 = os.path.join(TEST_DATA_PATH, f"wasm-tools-{WASM_TOOLS_VERSION}-x86_64-linux")

WASM_TOOLS_URL_AARCH64 = f"https://github.com/bytecodealliance/wasm-tools/releases/download/v{WASM_TOOLS_VERSION}/wasm-tools-{WASM_TOOLS_VERSION}-aarch64-linux.tar.gz"
WASM_TOOLS_DOWNLOADED_FILE_AARCH64 = os.path.join(TEST_DATA_PATH, f"wasm-tools-{WASM_TOOLS_VERSION}-aarch64-linux.tar.gz")
WASM_TOOLS_DOWNLOADED_DIR_AARCH64 = os.path.join(TEST_DATA_PATH, f"wasm-tools-{WASM_TOOLS_VERSION}-aarch64-linux")

WASM_TOOLS_DIR_PATH = os.path.join(TEST_DATA_PATH, "wasm-tools")
WASM_TOOLS_PATH = os.path.join(WASM_TOOLS_DIR_PATH, "wasm-tools")

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

def check_executable(exe: str):
    try:
        subprocess.run([exe, "--version"], check=True, capture_output=True)
    except subprocess.CalledProcessError:
        print(f"{exe} is not installed")
        exit(1)

check_executable("wget")
check_executable("tar")
check_executable("git")

os.makedirs(TEST_DATA_PATH, exist_ok=True)

if not os.path.exists(WASM_TOOLS_DIR_PATH):
    if platform.processor() == "x86_64":
        subprocess.run(["wget", "-P", TEST_DATA_PATH, WASM_TOOLS_URL_X86_64])
        subprocess.run(["tar", "-xvf", WASM_TOOLS_DOWNLOADED_FILE_X86_64, "-C", TEST_DATA_PATH])
        subprocess.run(["mv", WASM_TOOLS_DOWNLOADED_DIR_X86_64, WASM_TOOLS_DIR_PATH])
    elif platform.processor() == "aarch64":
        subprocess.run(["wget", "-P", TEST_DATA_PATH, WASM_TOOLS_URL_AARCH64])
        subprocess.run(["tar", "-xvf", WASM_TOOLS_DOWNLOADED_FILE_AARCH64, "-C", TEST_DATA_PATH])
        subprocess.run(["mv", WASM_TOOLS_DOWNLOADED_DIR_AARCH64, WASM_TOOLS_DIR_PATH])
    else:
        print("Unsupported platform")
        exit(1)

if not os.path.exists(TESTSUITE_SOURCE_PATH):
    subprocess.run(["git", "clone", "https://github.com/WebAssembly/testsuite", TESTSUITE_SOURCE_PATH])
    if TESTSUITE_PINNED:
        subprocess.run(["git", "-C", TESTSUITE_SOURCE_PATH, "reset", "--hard", TESTSUITE_COMMIT])

shutil.rmtree(TESTSUITE_PROCESSED_PATH, ignore_errors=True)

for file in os.listdir(TESTSUITE_SOURCE_PATH):
    if not file.endswith(".wast"):
        continue

    test_name = file.removesuffix(".wast")
    test_directory = os.path.join(TESTSUITE_PROCESSED_PATH, test_name)
    wast_path = os.path.join(TESTSUITE_SOURCE_PATH, file)

    os.makedirs(test_directory)
    subprocess.run([WASM_TOOLS_PATH, "json-from-wast", wast_path, f"--output={os.path.join(test_directory, f'{test_name}.json')}", f"--wasm-dir={test_directory}"])

for proposal, flag in ENABLED_PROPOSALS:
    proposal_path = os.path.join(TESTSUITE_SOURCE_PATH, "proposals", proposal)
    for file in os.listdir(proposal_path):
        if not file.endswith(".wast"):
            continue

        test_name = file.removesuffix(".wast")
        test_directory = os.path.join(TESTSUITE_PROCESSED_PATH, "proposals", proposal, test_name)
        wast_path = os.path.join(TESTSUITE_SOURCE_PATH, "proposals", proposal, file)

        os.makedirs(test_directory)
        subprocess.run([WASM_TOOLS_PATH, "json-from-wast", wast_path, flag, f"--output={os.path.join(test_directory, f'{test_name}.json')}", f"--wasm-dir={test_directory}"])
