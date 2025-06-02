#include <Module.h>
#include <VM.h>
#include <cstring>

Memory::Memory(const WasmFile::Memory& memory)
    : size(memory.limits.min)
    , max(memory.limits.max)
{
    data = (uint8_t*)malloc(size * WASM_PAGE_SIZE);
    memset(data, 0, size * WASM_PAGE_SIZE);
}

Memory::~Memory()
{
    free(data);
}

Table::Table(Type type)
    : type(type)
{
}

Global::Global(Type type, WasmFile::GlobalMutability mut, Value value)
    : type(type)
    , mut(mut)
    , value(value)
{
}

Module::Module(size_t id)
    : id(id)
{
}

void Module::add_table(Ref<Table> table)
{
    tables.push_back(table);
}

Ref<Table> Module::get_table(uint32_t index) const
{
    if (index >= tables.size())
        throw Trap();

    return tables[index];
}

void Module::add_memory(Ref<Memory> memory)
{
    memories.push_back(memory);
}

Ref<Memory> Module::get_memory(uint32_t index) const
{
    if (index >= memories.size())
        throw Trap();

    return memories[index];
}

void Module::add_global(Ref<Global> global)
{
    globals.push_back(global);
}

Ref<Global> Module::get_global(uint32_t index) const
{
    if (index >= globals.size())
        throw Trap();

    return globals[index];
}
