#pragma once

#include <JIT.h>
#include <Module.h>

typedef void (*JITCode)(Value* locals, Value* returnValue);

struct JITCompilationException : public std::exception
{
    const char* what() const throw()
    {
        return "JITCompilationException";
    }
};

class Compiler
{
public:
    static JITCode compile(Ref<const Function> function, Ref<WasmFile::WasmFile> wasmFile);

private:
    static void get_local(uint32_t localIndex);

    static void push_value(Value::Type type, JIT::Operand arg);
    static void pop_value(JIT::Operand arg);

    static constexpr auto GPR0 = JIT::Reg::RAX;
    static constexpr auto GPR1 = JIT::Reg::RCX;
    static constexpr auto FUNCTION_TEMPORARY = JIT::Reg::R8;

    static constexpr auto ARG0 = JIT::Reg::RDI;
    static constexpr auto ARG1 = JIT::Reg::RSI;
    static constexpr auto ARG2 = JIT::Reg::RDX;

    static constexpr auto STACK_POINTER = JIT::Reg::RSP;
    static constexpr auto BEGIN_STACK_POINTER = JIT::Reg::R13;

    static constexpr auto RETURN_VALUE_REGISTER = JIT::Reg::R14;
    static constexpr auto LOCALS_ARRAY_REGISTER = JIT::Reg::R15;

    static inline JIT m_jit;
    static inline Ref<const Function> m_function;
    static inline std::vector<Value::Type> m_local_types;
};
