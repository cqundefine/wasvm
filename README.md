# wasvm
wasvm is very simple C++ implementation of WASVM bytecode interpreter written in modern C++23.

## Building on Linux
**Ubuntu 24.04 and later are the only supported distributions. You will probably have success on other ones from 2024 and later** \
To build this you need a bleeding edge C++ compiler and standard library. On Ubuntu 24.04+ you will need to install `clang-21` as the compiler as it's the only one able to compile this (on Ubuntu 24.04 you can get it from the LLVM APT repository). For the standard library make sure you have `g++14` installed as it comes with a new enough `libstdc++`. \
For other packages:
```bash
sudo apt install gcc-14 g++-14 cmake ninja-build
```
Then run those commands:
```bash
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21
ninja
```

## Running the WASM testsuite
You will need some additional packages, on Ubuntu run:
```bash
sudo apt install python3 wget tar git
```
The scripts expect a wasvm binary in the root of the project. If you followed the inscrutions before run:
```bash
ln -s build/wasvm wasvm
```
Then to prepare the testsuite run the `make_tests.py` script. If you wish to run the full testsuite use the `run_tests.py` script. If you instead want to run only a part of the suite, use the `-t` flag. For example
```bash
./wasvm -t i32
```
to run the `i32.wast` file.
