#pragma once

#include <Value.h>
#include <WasmFile.h>

struct Module;

struct Function
{
    // FIXME: Storing this these as values is a bad idea
    WasmFile::FunctionType type;
    Ref<Module> mod;
    WasmFile::Code code;
};

struct Memory
{
    uint8_t* data;
    uint32_t size;
    std::optional<uint32_t> max;

    Memory(const WasmFile::Memory& memory);
};

struct Table
{
    std::vector<Reference> elements;
    std::optional<uint32_t> max;
};

struct Global
{
    Value value;

    Global(Value value);
};

class Module
{
public:
    Ref<WasmFile::WasmFile> wasmFile;
    std::vector<Ref<Function>> functions;

    void add_table(Ref<Table> table);
    Ref<Table> get_table(uint32_t index) const;

    void add_memory(Ref<Memory> memory);
    Ref<Memory> get_memory(uint32_t index) const;

    void add_global(Ref<Global> global);
    Ref<Global> get_global(uint32_t index) const;

private:
    std::vector<Ref<Table>> tables;
    std::vector<Ref<Memory>> memories;
    std::vector<Ref<Global>> globals;
};
