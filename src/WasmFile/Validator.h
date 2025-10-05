#pragma once

#include "VM/Type.h"
#include "WasmFile.h"

class Validator
{
public:
    static constexpr uint64_t MAX_WASM_PAGES_I32 = 0x10000;
    static constexpr uint64_t MAX_WASM_PAGES_I64 = 0x1000000000000;

    Validator(Ref<WasmFile::WasmFile> wasmFile);

private:
    void validate_function(const WasmFile::FunctionType& type, WasmFile::Code& code);
    void validate_constant_expression(const std::vector<Instruction>& instructions, Type expectedReturnType, bool globalRestrictions);
    Value run_global_restricted_constant_expression(const std::vector<Instruction>& instructions);

    Ref<WasmFile::WasmFile> m_wasmFile;

    uint32_t m_imported_global_count { 0 };
    std::vector<std::pair<Type, WasmFile::GlobalMutability>> m_globals;

    std::vector<AddressType> m_memories;
    std::vector<std::pair<Type, AddressType>> m_tables;

    std::vector<uint32_t> m_functions;
    std::vector<uint32_t> m_declared_functions;
};
