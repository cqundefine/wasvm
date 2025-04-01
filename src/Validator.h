#pragma once

#include <WasmFile.h>

class Validator
{
public:
    Validator(Ref<WasmFile::WasmFile> wasmFile);

private:
    void validate_function(const WasmFile::FunctionType& type, WasmFile::Code& code);
    void validate_constant_expression(const std::vector<Instruction>& instructions, Type expectedReturnType, bool globalRestrictions);
    Value run_global_restricted_constant_expression(const std::vector<Instruction>& instructions);

    Ref<WasmFile::WasmFile> m_wasmFile;

    std::vector<uint32_t> m_functions;
    uint32_t m_imported_global_count { 0 };
    std::vector<std::pair<Type, WasmFile::GlobalMutability>> m_globals;
    uint32_t m_memories { 0 };
    std::vector<Type> m_tables;
    std::vector<uint32_t> m_declared_functions;
};
