#include "WasmFile.h"
#include <Module.h>
#include <VM.h>
#include <cstring>

Memory::Memory(const WasmFile::Memory& memory)
    : m_size(memory.limits.min)
    , m_max(memory.limits.max)
{
    m_data = new uint8_t[m_size * WASM_PAGE_SIZE];
    memset(m_data, 0, m_size * WASM_PAGE_SIZE);
}

Memory::~Memory()
{
    delete[] m_data;
}

WasmFile::Limits Memory::limits() const
{
    return WasmFile::Limits(m_size, m_max);
}

void Memory::grow(uint32_t pages)
{
    uint32_t newSize = (m_size + pages) * WASM_PAGE_SIZE;

    uint8_t* newMemory = new uint8_t[newSize];
    memset(newMemory, 0, newSize);

    memcpy(newMemory, m_data, m_size * WASM_PAGE_SIZE);

    delete[] m_data;
    m_data = newMemory;
    m_size += pages;
}

Table::Table(const WasmFile::Table& table, Reference initialValue)
    : m_type(table.refType)
{
    m_max = table.limits.max;
    m_elements.reserve(table.limits.min);
    for (uint32_t i = 0; i < table.limits.min; i++)
        m_elements.push_back(initialValue);
}

WasmFile::Limits Table::limits() const
{
    return WasmFile::Limits(m_elements.size(), m_max);
}

void Table::grow(uint32_t elements, Reference value)
{
    m_elements.reserve(m_elements.size() + elements);

    for (uint32_t i = 0; i < elements; i++)
        m_elements.push_back(value);
}

Reference Table::get(uint32_t index) const
{
    if (index >= m_elements.size())
        throw Trap();

    return m_elements[index];
}

void Table::set(uint32_t index, Reference element)
{
    if (index >= m_elements.size())
        throw Trap();

    m_elements[index] = element;
}

Reference Table::unsafe_get(uint32_t index) const
{
    return m_elements[index];
}

void Table::unsafe_set(uint32_t index, Reference element)
{
    m_elements[index] = element;
}

Global::Global(Type type, WasmFile::GlobalMutability mutability, Value defaultValue)
    : m_type(type)
    , m_mutability(mutability)
    , m_value(defaultValue)
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
#ifdef DEBUG_BUILD
    if (index >= tables.size())
        throw Trap();
#endif

    return tables[index];
}

void Module::add_memory(Ref<Memory> memory)
{
    memories.push_back(memory);
}

Ref<Memory> Module::get_memory(uint32_t index) const
{
#ifdef DEBUG_BUILD
    if (index >= memories.size())
        throw Trap();
#endif

    return memories[index];
}

void Module::add_global(Ref<Global> global)
{
    globals.push_back(global);
}

Ref<Global> Module::get_global(uint32_t index) const
{
#ifdef DEBUG_BUILD
    if (index >= globals.size())
        throw Trap();
#endif

    return globals[index];
}
