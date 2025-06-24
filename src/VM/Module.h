#pragma once

#include "Value.h"
#include "WasmFile/WasmFile.h"

class Function
{
public:
    virtual const WasmFile::FunctionType& type() const = 0;
    [[nodiscard]] virtual std::vector<Value> run(std::span<const Value> args) const = 0;
};

struct RealModule;

class RealFunction : public Function
{
public:
    constexpr RealFunction(WasmFile::FunctionType* type, WasmFile::Code* code, Ref<RealModule> parent)
        : m_type(type)
        , m_code(code)
        , m_parent(parent)
    {
    }

    virtual const WasmFile::FunctionType& type() const override;
    const WasmFile::Code& code() const { return *m_code; }
    Ref<RealModule> parent() const { return m_parent.lock(); }

    [[nodiscard]] virtual std::vector<Value> run(std::span<const Value> args) const override;

private:
    WasmFile::FunctionType* m_type;
    WasmFile::Code* m_code;
    Weak<RealModule> m_parent;
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

using ImportedObject = std::variant<Ref<Function>, Ref<Table>, Ref<Memory>, Ref<Global>>;

class Module
{
public:
    virtual Ref<Table> get_table(uint32_t index) const = 0;
    virtual Ref<Memory> get_memory(uint32_t index) const = 0;
    virtual Ref<Global> get_global(uint32_t index) const = 0;

    virtual Ref<Function> get_function(std::string_view name) const = 0;

    virtual std::optional<ImportedObject> try_import(std::string_view name, WasmFile::ImportType type) const = 0;
};

class RealModule : public Module
{
public:
    RealModule(size_t id, Ref<WasmFile::WasmFile> wasmFile);

    size_t id() const { return m_id; }
    Ref<WasmFile::WasmFile> wasm_file() const { return m_wasm_file; }

    void add_table(Ref<Table> table);
    virtual Ref<Table> get_table(uint32_t index) const override;

    void add_memory(Ref<Memory> memory);
    virtual Ref<Memory> get_memory(uint32_t index) const override;

    void add_global(Ref<Global> global);
    virtual Ref<Global> get_global(uint32_t index) const override;

    void add_function(Ref<Function> function);
    Ref<Function> get_function(uint32_t index) const;
    virtual Ref<Function> get_function(std::string_view name) const override;

    std::optional<Ref<Function>> start_function() const;

    virtual std::optional<ImportedObject> try_import(std::string_view name, WasmFile::ImportType type) const override;

private:
    size_t m_id;
    Ref<WasmFile::WasmFile> m_wasm_file;

    std::vector<Ref<Function>> m_functions;
    std::vector<Ref<Table>> m_tables;
    std::vector<Ref<Memory>> m_memories;
    std::vector<Ref<Global>> m_globals;
};
