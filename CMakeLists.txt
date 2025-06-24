cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

project(WASVM)

add_compile_options(-Wimplicit-fallthrough -Wno-user-defined-literals -Wno-explicit-specialization-storage-class -Wno-deprecated-declarations)
add_link_options(-static-libgcc -static-libstdc++)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    add_compile_options(-march=x86-64-v2)
endif()

# add_compile_options(-fsanitize=address -fsanitize=undefined)
# add_link_options(-fsanitize=address -fsanitize=undefined)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG_BUILD)
endif()

file(GLOB_RECURSE SOURCES src/*.cpp external/*.cpp)
add_executable(wasvm ${SOURCES})

target_include_directories(wasvm PUBLIC src)
target_include_directories(wasvm PUBLIC external)
