#include "SpecTestModule.h"
#include "VM/Module.h"
#include "VM/Type.h"
#include "WasmFile/WasmFile.h"

class EmptyFunction final : public Function
{
public:
    EmptyFunction(const std::vector<Type>& params)
        : m_type(WasmFile::FunctionType {
              .params = params,
              .returns = {} })
    {
    }

    virtual const WasmFile::FunctionType& type() const override
    {
        return m_type;
    }

    virtual std::vector<Value> run(std::span<const Value>) const override
    {
        return {};
    }

private:
    WasmFile::FunctionType m_type;
};

SpecTestModule::SpecTestModule()
{
    m_globals["global_i32"] = MakeRef<Global>(Type::i32, WasmFile::GlobalMutability::Constant, static_cast<uint32_t>(666));
    m_globals["global_i64"] = MakeRef<Global>(Type::i64, WasmFile::GlobalMutability::Constant, static_cast<uint64_t>(666));
    m_globals["global_f32"] = MakeRef<Global>(Type::f32, WasmFile::GlobalMutability::Constant, static_cast<float>(666.6));
    m_globals["global_f64"] = MakeRef<Global>(Type::f64, WasmFile::GlobalMutability::Constant, static_cast<double>(666.6));

    m_table = MakeRef<Table>(WasmFile::Table {
                                 .refType = Type::funcref,
                                 .limits = WasmFile::Limits {
                                     .min = 10,
                                     .max = 20,
                                     .address_type = AddressType::i32 } },
        Reference { ReferenceType::Function, {}, nullptr });

    m_table64 = MakeRef<Table>(WasmFile::Table {
                                   .refType = Type::funcref,
                                   .limits = WasmFile::Limits {
                                       .min = 10,
                                       .max = 20,
                                       .address_type = AddressType::i64 } },
        Reference { ReferenceType::Function, {}, nullptr });

    m_memory = MakeRef<Memory>(WasmFile::Memory {
        .limits = WasmFile::Limits {
            .min = 1,
            .max = 2 } });

    m_functions["print"] = MakeRef<EmptyFunction>(std::vector<Type> {});
    m_functions["print_i32"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::i32 });
    m_functions["print_i64"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::i64 });
    m_functions["print_f32"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::f32 });
    m_functions["print_f64"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::f64 });
    m_functions["print_i32_f32"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::i32, Type::f32 });
    m_functions["print_f64_f64"] = MakeRef<EmptyFunction>(std::vector<Type> { Type::f64, Type::f64 });
}

std::optional<ImportedObject> SpecTestModule::try_import(std::string_view name, WasmFile::ImportType type) const
{
    switch (type)
    {
        using enum WasmFile::ImportType;
        case Function: {
            auto it = m_functions.find(name);
            return it != m_functions.end() ? it->second : std::optional<ImportedObject> {};
        }
        case Table:
            if (name == "table")
                return m_table;
            if (name == "table64")
                return m_table64;
            return std::optional<ImportedObject> {};
        case Memory:
            return name == "memory" ? m_memory : std::optional<ImportedObject> {};
        case Global: {
            auto it = m_globals.find(name);
            return it != m_globals.end() ? it->second : std::optional<ImportedObject> {};
        }
    }
}
