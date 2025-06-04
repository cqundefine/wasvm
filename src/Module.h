#pragma once

#include <Parser.h>
#include <Value.h>
#include <WasmFile.h>

struct Module;

class Function
{
public:
    constexpr Function(WasmFile::FunctionType* type, WasmFile::Code* code, Ref<Module> parent)
        : m_type(type)
        , m_code(code)
        , m_parent(parent)
    {
    }

    const WasmFile::FunctionType& type() const { return *m_type; }
    const WasmFile::Code& code() const { return *m_code; }
    Ref<Module> parent() const { return m_parent.lock(); }

private:
    WasmFile::FunctionType* m_type;
    WasmFile::Code* m_code;
    Weak<Module> m_parent;
};

class Memory
{
public:
    Memory(const WasmFile::Memory& memory);
    ~Memory();

    WasmFile::Limits limits() const;

    void grow(uint32_t pages);

    uint8_t* data() const { return m_data; }
    uint32_t size() const { return m_size; }
    std::optional<uint32_t> max() const { return m_max; }

private:
    uint8_t* m_data;

    uint32_t m_size;
    std::optional<uint32_t> m_max;
};

struct Table
{
public:
    Table(const WasmFile::Table& table, Reference initialValue);

    WasmFile::Limits limits() const;

    void grow(uint32_t elements, Reference value);

    Reference get(uint32_t index) const;
    void set(uint32_t index, Reference element);

    Reference unsafe_get(uint32_t index) const;
    void unsafe_set(uint32_t index, Reference element);

    Type type() const { return m_type; }
    uint32_t size() const { return static_cast<uint32_t>(m_elements.size()); }
    std::optional<uint32_t> max() const { return m_max; }

private:
    std::vector<Reference> m_elements;

    Type m_type;
    std::optional<uint32_t> m_max;
};

struct Global
{
public:
    Global(Type type, WasmFile::GlobalMutability mutability, Value defaultValue);

    Value get() const { return m_value; }
    void set(Value value) { m_value = value; }

    Type type() const { return m_type; }
    WasmFile::GlobalMutability mutability() const { return m_mutability; }

private:
    Value m_value;

    Type m_type;
    WasmFile::GlobalMutability m_mutability;
};

class Module
{
public:
    Ref<WasmFile::WasmFile> wasmFile;
    std::vector<Ref<Function>> functions;
    size_t id;

    Module(size_t id);

    // FIXME: Do these actually need a function, because the validator guarantees us correctness
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
