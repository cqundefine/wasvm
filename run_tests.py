#!/usr/bin/python3

import os
import subprocess
import json

GREEN = "\u001b[38;5;10m"
YELLOW = "\u001b[38;5;11m"
RED = "\u001b[38;5;9m"
DARK_RED = "\u001b[38;5;1m"
RESET = "\u001b[0m"

def colored(text: str, color: str) -> str:
    return f"{color}{text}{RESET}"

total = 0
passed = 0
failed = 0
skipped = 0
failed_to_load = 0

crashes = []

tests = os.listdir("tests")
tests.sort()
for filename in tests:
    if filename == "linking":
        continue
    if os.path.exists("tests/" + filename + "/" + filename + ".json"):
        process = subprocess.run(["./wasvm", "-t", filename], capture_output=True)
        if process.returncode != 0:
            print(f"{filename:<30} {colored("vm crashed", DARK_RED)}")
            crashes.append(filename)
        else:
            data = json.loads(process.stdout.decode().splitlines()[-1])
            if data["passed"] == data["total"]:
                pass
                # print(f'{filename:<30} {colored("all passed", GREEN)}')
            else:
                print(f'{filename:<30} {data["total"]}/{colored(data["passed"], GREEN)}/{colored(data["failed"], RED)}/{colored(data["skipped"], YELLOW)}/{colored(data["failed_to_load"], DARK_RED)}')
            total += data["total"]
            passed += data["passed"]
            failed += data["failed"]
            skipped += data["skipped"]
            failed_to_load += data["failed_to_load"]

print("-------------------------------------------")
print(f"{"Total:":<30} {total}", )
print(f"{"Passed:":<30} {colored(passed, GREEN)}")
print(f"{"Failed:":<30} {colored(failed, RED)}")
print(f"{"Skipped:":<30} {colored(skipped, YELLOW)}")
print(f"{"Failed to load:":<30} {colored(failed_to_load, DARK_RED)}")
if len(crashes) != 0:
    print("Crashes:")
    for crash in crashes:
        print(f"- {crash}")
