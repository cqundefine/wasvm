#!/usr/bin/python3

import os
import subprocess
import json

GREEN = "\u001b[38;5;10m"
YELLOW = "\u001b[38;5;11m"
RED = "\u001b[38;5;9m"
DARK_RED = "\u001b[38;5;1m"
RESET = "\u001b[0m"

TEST_DATA_PATH = "test_data"
TESTSUITE_PROCESSED_PATH = os.path.join(TEST_DATA_PATH, "testsuite-processed")
if not TESTSUITE_PROCESSED_PATH.endswith("/"):
    TESTSUITE_PROCESSED_PATH += "/"

def colored(text: str, color: str) -> str:
    return f"{color}{text}{RESET}"

total = 0
passed = 0
failed = 0
skipped = 0
failed_to_load = 0
vm_error = 0

crashes = []

tests: list[str] = []
for root, dirs, files in os.walk(TESTSUITE_PROCESSED_PATH):
    if root == TESTSUITE_PROCESSED_PATH:
        continue
    tests.append(root.removeprefix(TESTSUITE_PROCESSED_PATH))
tests.sort()

for filename in tests:
    if os.path.exists(os.path.join(TESTSUITE_PROCESSED_PATH, filename, filename.split("/")[-1] + ".json")):
        process = subprocess.run(["build/wasvm", "-t", filename], capture_output=True)
        if process.returncode != 0:
            print(f"{filename:<50} {colored("vm crashed", DARK_RED)}")
            crashes.append(filename)
        else:
            data = json.loads(process.stdout.decode().splitlines()[-1])
            if data["vm_error"]:
                vm_error += 1
                print(f"{filename:<50} {colored("vm error", DARK_RED)}")
                continue
            if data["passed"] == data["total"]:
                pass
                # print(f'{filename:<50} {colored("all passed", GREEN)}')
            else:
                print(f'{filename:<50} {data["total"]}/{colored(data["passed"], GREEN)}/{colored(data["failed"], RED)}/{colored(data["skipped"], YELLOW)}/{colored(data["failed_to_load"], DARK_RED)}')
            total += data["total"]
            passed += data["passed"]
            failed += data["failed"]
            skipped += data["skipped"]
            failed_to_load += data["failed_to_load"]

print("-------------------------------------------")
print(f"{"Total:":<50} {total}", )
print(f"{"Passed:":<50} {colored(passed, GREEN)}")
print(f"{"Failed:":<50} {colored(failed, RED)}")
print(f"{"Skipped:":<50} {colored(skipped, YELLOW)}")
print(f"{"Failed to load:":<50} {colored(failed_to_load, DARK_RED)}")
print(f"{"VM errors:":<50} {colored(vm_error, DARK_RED)}")
if len(crashes) != 0:
    print("Crashes:")
    for crash in crashes:
        print(f"- {crash}")
