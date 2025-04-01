#include <Compiler.h>
#include <JIT.h>
#include <Opcode.h>
#include <Parser.h>
#include <VM.h>
#include <utility>

__attribute((sysv_abi)) void block(size_t continuation, uint32_t arity, size_t stackHeight)
{
    // VM::frame()->label_stack.push_back(Label {
    //     .continuation = continuation,
    //     .arity = arity,
    //     .stackHeight = stackHeight });
}

__attribute((sysv_abi)) void end()
{
    // VM::frame()->label_stack.pop_back();
}

__attribute((sysv_abi)) void branch_to_label(uint32_t index)
{
    // FIXME: Does this need to be a loop
    // Label label;
    // for (uint32_t i = 0; i < index + 1; i++)
    // {
    //     label = VM::frame()->label_stack.back();
    //     VM::frame()->label_stack.pop_back();
    // }
}

JITCode Compiler::compile(Ref<Function> function, Ref<WasmFile::WasmFile> wasmFile)
{
    m_jit = {};
    m_function = function;

    m_local_types.clear();
    for (const auto& param : function->type.params)
        m_local_types.push_back(value_type_from_type(param));
    for (const auto& local : function->code.locals)
        m_local_types.push_back(value_type_from_type(local));

    if (function->type.returns.size() > 1)
    {
        std::println(std::cerr, "Error: More than one return value is not supported in JIT");
        throw JITCompilationException();
    }

    m_jit.mov64(JIT::Operand::Register(LOCALS_ARRAY_REGISTER), JIT::Operand::Register(ARG0));
    m_jit.mov64(JIT::Operand::Register(RETURN_VALUE_REGISTER), JIT::Operand::Register(ARG1));

    for (const auto& instruction : function->code.instructions)
    {
        switch (instruction.opcode)
        {
            case Opcode::nop:
                m_jit.nop();
                break;
            case Opcode::block:
            case Opcode::loop: {
                const auto& arguments = instruction.get_arguments<BlockLoopArguments>();

                // TODO: continuation = rip

                // arity = label.arity
                m_jit.mov32(JIT::Operand::Register(ARG1), JIT::Operand::Immediate(arguments.label.arity));

                // stackHeight = rsp - paramCount
                m_jit.mov64(JIT::Operand::Register(ARG2), JIT::Operand::Register(JIT::Reg::RSP));
                m_jit.sub64(JIT::Operand::Register(ARG2), JIT::Operand::Immediate(arguments.blockType.get_param_types(function->mod->wasmFile).size() * 2 * sizeof(uint64_t)));

                m_jit.native_call((void*)block);
                break;
            }
            case Opcode::end:
                m_jit.native_call((void*)end);
                break;
            case Opcode::br:

            case Opcode::local_get:
                get_local(instruction.get_arguments<uint32_t>());
                break;
            case Opcode::i32_const:
                m_jit.mov32(JIT::Operand::Register(GPR0), JIT::Operand::Immediate(instruction.get_arguments<uint32_t>()));
                push_value(Value::Type::UInt32, JIT::Operand::Register(GPR0));
                break;
            case Opcode::i64_const:
                m_jit.mov64(JIT::Operand::Register(GPR0), JIT::Operand::Immediate(instruction.get_arguments<uint64_t>()));
                push_value(Value::Type::UInt64, JIT::Operand::Register(GPR0));
                break;
            case Opcode::i32_add:
                pop_value(JIT::Operand::Register(GPR0));
                pop_value(JIT::Operand::Register(GPR1));
                m_jit.add32(JIT::Operand::Register(GPR0), JIT::Operand::Register(GPR1));
                push_value(Value::Type::UInt32, JIT::Operand::Register(GPR0));
                break;
            case Opcode::i64_add:
                pop_value(JIT::Operand::Register(GPR0));
                pop_value(JIT::Operand::Register(GPR1));
                m_jit.add64(JIT::Operand::Register(GPR0), JIT::Operand::Register(GPR1));
                push_value(Value::Type::UInt64, JIT::Operand::Register(GPR0));
                break;
            default:
                std::println(std::cerr, "Error: Opcode {:#x} not supported in JIT", std::to_underlying(instruction.opcode));
                throw JITCompilationException();
        }
    }

    // FIXME: This is very hacky
    if (function->type.returns.size() == 1)
    {
        pop_value(JIT::Operand::Register(GPR0));

        if (function->type.returns[0] == Type::i32)
            m_jit.mov32(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::data_offset()), JIT::Operand::Register(GPR0));
        else if (function->type.returns[0] == Type::i64)
            m_jit.mov64(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::data_offset()), JIT::Operand::Register(GPR0));
        else
        {
            std::println(std::cerr, "Error: Unsupported value type in JIT");
            throw JITCompilationException();
        }

        m_jit.mov64(JIT::Operand::Register(FUNCTION_TEMPORARY), JIT::Operand::Immediate(static_cast<uint64_t>(value_type_from_type(function->type.returns[0]))));
        m_jit.mov64(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::type_offset()), JIT::Operand::Register(FUNCTION_TEMPORARY));
    }
    m_jit.exit();

    return (JITCode)m_jit.build();
}

void Compiler::get_local(uint32_t index)
{
    uint64_t localDataOffset = index * sizeof(Value) + Value::data_offset();

    if (m_local_types[index] == Value::Type::UInt32)
        m_jit.mov32(JIT::Operand::Register(GPR0), JIT::Operand::MemoryBaseAndOffset(LOCALS_ARRAY_REGISTER, localDataOffset));
    else if (m_local_types[index] == Value::Type::UInt64)
        m_jit.mov64(JIT::Operand::Register(GPR0), JIT::Operand::MemoryBaseAndOffset(LOCALS_ARRAY_REGISTER, localDataOffset));
    else
    {
        std::println(std::cerr, "Error: Unsupported value type in JIT");
        throw JITCompilationException();
    }

    push_value(m_local_types[index], JIT::Operand::Register(GPR0));
}

void Compiler::push_value(Value::Type type, JIT::Operand arg)
{
    m_jit.mov64(JIT::Operand::Register(FUNCTION_TEMPORARY), JIT::Operand::Immediate(static_cast<uint64_t>(type)));
    m_jit.push64(JIT::Operand::Register(FUNCTION_TEMPORARY));
    m_jit.push64(arg);
}

void Compiler::pop_value(JIT::Operand arg)
{
    m_jit.pop64(arg);
    m_jit.pop64(JIT::Operand::Register(FUNCTION_TEMPORARY)); // Just ignore the type
}
