cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

project(WASVM)

add_compile_options(-fdiagnostics-color=always)

add_compile_options(-Wimplicit-fallthrough -Wswitch-default -Wno-user-defined-literals -Wno-explicit-specialization-storage-class -march=x86-64-v2)

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(wasvm ${SOURCES})

target_include_directories(wasvm PUBLIC src)
