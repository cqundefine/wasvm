#include "WasmFile.h"
#include <Module.h>
#include <VM.h>
#include <cstring>
#include <optional>
#include <utility>

const WasmFile::FunctionType& RealFunction::type() const
{
    return *m_type;
}

std::vector<Value> RealFunction::run(std::span<const Value> args) const
{
    return VM::run_function(m_parent.lock(), this, args);
}

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

RealModule::RealModule(size_t id, Ref<WasmFile::WasmFile> wasmFile)
    : m_id(id)
    , m_wasm_file(wasmFile)
{
}

void RealModule::add_table(Ref<Table> table)
{
    m_tables.push_back(table);
}

Ref<Table> RealModule::get_table(uint32_t index) const
{
#ifdef DEBUG_BUILD
    if (index >= m_tables.size())
        throw Trap();
#endif

    return m_tables[index];
}

void RealModule::add_memory(Ref<Memory> memory)
{
    m_memories.push_back(memory);
}

Ref<Memory> RealModule::get_memory(uint32_t index) const
{
#ifdef DEBUG_BUILD
    if (index >= m_memories.size())
        throw Trap();
#endif

    return m_memories[index];
}

void RealModule::add_global(Ref<Global> global)
{
    m_globals.push_back(global);
}

Ref<Global> RealModule::get_global(uint32_t index) const
{
#ifdef DEBUG_BUILD
    if (index >= m_globals.size())
        throw Trap();
#endif

    return m_globals[index];
}

void RealModule::add_function(Ref<Function> function)
{
    m_functions.push_back(function);
}

Ref<Function> RealModule::get_function(uint32_t index) const
{
    return m_functions[index];
}

Ref<Function> RealModule::get_function(std::string_view name) const
{
    const auto functionExport = m_wasm_file->find_export_by_name(name);
    if (!functionExport.has_value())
        throw Trap();

#ifdef DEBUG_BUILD
    if (functionExport.value().type != WasmFile::ImportType::Function)
        throw Trap();
#endif

    return m_functions[functionExport.value().index];
}

std::optional<Ref<Function>> RealModule::start_function() const
{
    if (m_wasm_file->startFunction.has_value())
        return m_functions[*m_wasm_file->startFunction];
    return {};
}

std::optional<ImportedObject> RealModule::try_import(std::string_view name, WasmFile::ImportType type) const
{
    const auto maybeExported = m_wasm_file->find_export_by_name(name);
    if (!maybeExported.has_value())
        return {};

    const auto exported = maybeExported.value();

    if (type != exported.type)
        return {};

    switch (type)
    {
        using enum WasmFile::ImportType;
        case Function:
            return m_functions[exported.index];
        case Table:
            return m_tables[exported.index];
        case Memory:
            return m_memories[exported.index];
        case Global:
            return m_globals[exported.index];
        default:
            std::unreachable();
    }
}
