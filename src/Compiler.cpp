#include <stdio.h>
#include <Compiler.h>
#include <JIT.h>
#include <Opcode.h>
#include <Parser.h>

void block(uint64_t* stack, uint64_t paramCount, Label label)
{
    // Value* values = new Value[paramCount];

    // for (uint64_t i = 0; i < paramCount; i++)
    // {
    //     Value::Type type = static_cast<Value::Type>(*stack);
    //     stack++;
    //     if (type == Value::Type::UInt32)
    //         values[i] = Value((uint32_t)*stack);
    //     else if (type == Value::Type::UInt64)
    //         values[i] = Value(*stack);
    //     else
    //     {
    //         fprintf(stderr, "Unsupported type in block params during JIT execution\n");
    //         assert(false);
    //     }

    //     stack++;
    // }

    // stack--;
    // *stack = std::bit_cast<uint64_t>(label);

    // if (paramCount > 0)
    //     for (uint64_t i = paramCount - 1; i >= 0; i--)
    //     {
    //         stack--;
    //         *stack = static_cast<uint64_t>(values[i].get_type());
    //         stack--;
    //         if (values[i].get_type() == Value::Type::UInt32)
    //             *stack = (uint64_t)values[i].get<uint32_t>();
    //         else if (values[i].get_type() == Value::Type::UInt64)
    //             *stack = values[i].get<uint64_t>();
    //         else
    //         {
    //             fprintf(stderr, "Unsupported type in block params during JIT execution\n");
    //             assert(false);
    //         }
    //     }
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
        fprintf(stderr, "More than one return value is not supported in JIT\n");
        throw JITCompilationException();
    }

    m_jit.mov64(JIT::Operand::Register(LOCALS_ARRAY_REGISTER), JIT::Operand::Register(ARG0));
    m_jit.mov64(JIT::Operand::Register(RETURN_VALUE_REGISTER), JIT::Operand::Register(ARG1));

    for (const auto& instruction : function->code.instructions)
    {
        switch (instruction.opcode)
        {
            case Opcode::block: {
                const BlockLoopArguments& arguments = std::get<BlockLoopArguments>(instruction.arguments);
                m_jit.mov64(JIT::Operand::Register(ARG0), JIT::Operand::Register(JIT::Reg::RSP));
                m_jit.mov64(JIT::Operand::Register(ARG1), JIT::Operand::Immediate(arguments.blockType.get_param_types(wasmFile).size()));
                // m_jit.mov64(JIT::Operand::Register(ARG2), JIT::Operand::Immediate(std::bit_cast<uint64_t>(arguments.label)));
                m_jit.native_call((void*)block);
                break;
            }
            case Opcode::end:
                // FIXME: This is very hacky and not correct at all
                if (function->type.returns.size() == 1)
                {
                    pop_value(JIT::Operand::Register(GPR0));

                    if (function->type.returns[0] == Type::i32)
                        m_jit.mov32(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::data_offset()), JIT::Operand::Register(GPR0));
                    else if (function->type.returns[0] == Type::i64)
                        m_jit.mov64(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::data_offset()), JIT::Operand::Register(GPR0));
                    else
                    {
                        fprintf(stderr, "Unsupported value type in JIT\n");
                        throw JITCompilationException();
                    }

                    m_jit.mov64(JIT::Operand::Register(FUNCTION_TEMPORARY), JIT::Operand::Immediate(static_cast<uint64_t>(value_type_from_type(function->type.returns[0]))));
                    m_jit.mov64(JIT::Operand::MemoryBaseAndOffset(RETURN_VALUE_REGISTER, Value::type_offset()), JIT::Operand::Register(FUNCTION_TEMPORARY));
                }
                m_jit.exit();
                break;
            case Opcode::local_get:
                get_local(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::i64_const:
                m_jit.mov64(JIT::Operand::Register(GPR0), JIT::Operand::Immediate(std::get<uint64_t>(instruction.arguments)));
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
                fprintf(stderr, "Opcode 0x%x not supported in JIT\n", instruction.opcode);
                throw JITCompilationException();
        }
    }

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
        fprintf(stderr, "Unsupported value type in JIT\n");
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
