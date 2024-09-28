#pragma once

#include <Value.h>
#include <WasmFile.h>

struct Memory
{
    uint8_t* data;
    uint32_t size;
    uint32_t max;
};

struct Table
{
    std::vector<Reference> elements;
    uint32_t max;
};

struct Global
{
    Value value;

    Global(Value value);
};

struct Module
{
    Ref<WasmFile::WasmFile> wasmFile;
    std::vector<Ref<Global>> globals;
    std::vector<Ref<Memory>> memories;
    std::vector<Ref<Table>> tables;
};
