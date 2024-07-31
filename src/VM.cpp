#include <MemoryStream.h>
#include <Opcode.h>
#include <Operators.h>
#include <Type.h>
#include <VM.h>
#include <WASI.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

void VM::bootup(WasmFile& file)
{
    m_wasmFile = file;

    m_globals.clear();

    // FIXME: For some reason m_memory becomes invalid
    // if (m_memory)
    // {
    //     m_memory_size = UINT32_MAX;
    //     free(m_memory);
    // }

    m_tables.clear();

    for (uint32_t i = 0; i < file.globals.size(); i++)
    {
        auto& global = file.globals.at(i);
        // FIXME: verify type
        m_globals[i] = run_bare_code_returning(global.initCode, (Type)global.type);
    }

    if (file.memories.size() > 0)
    {
        m_memory_size = file.memories.at(0).limits.min;
        m_memory = (uint8_t*)malloc(m_memory_size * WASM_PAGE_SIZE);
    }

    for (const auto& data : file.dataBlocks)
    {
        Value beginValue = run_bare_code_returning(data.expr, i32);
        assert(std::holds_alternative<uint32_t>(beginValue));
        uint32_t begin = std::get<uint32_t>(beginValue);

        memcpy(m_memory + begin, data.data.data(), data.data.size());
    }

    for (const auto& table : file.tables)
    {
        assert(table.limits.min <= table.limits.max);
        std::vector<uint32_t> tableVector;
        tableVector.reserve(table.limits.min);
        for (uint32_t i = 0; i < table.limits.min; i++)
            tableVector.push_back(UINT32_MAX);
        m_tables.push_back(tableVector);
    }

    for (const auto& element : file.elements)
    {
        if (element.type == 0)
        {
            Value beginValue = run_bare_code_returning(element.expr, i32);
            assert(std::holds_alternative<uint32_t>(beginValue));
            uint32_t begin = std::get<uint32_t>(beginValue);
            for (size_t i = 0; i < element.functionIndexes.size(); i++)
            {
                m_tables[0][begin + i] = element.functionIndexes.at(i);
            }
        }
        else if (element.type == 2)
        {
            Value beginValue = run_bare_code_returning(element.expr, i32);
            assert(std::holds_alternative<uint32_t>(beginValue));
            uint32_t begin = std::get<uint32_t>(beginValue);
            for (size_t i = 0; i < element.functionIndexes.size(); i++)
            {
                m_tables[element.table][begin + i] = element.functionIndexes.at(i);
            }
        }
        else
        {
            assert(false);
        }
    }
}

std::vector<Value> VM::run_function(const std::string& name, const std::vector<Value>& args)
{
    Export functionExport = m_wasmFile.find_export_by_name(name);
    assert(functionExport.type == 0);
    return run_function(functionExport.index, args);
}

std::vector<Value> VM::run_function(uint32_t index, const std::vector<Value>& args)
{
    return run_function_new_parser(m_wasmFile.functionTypes[m_wasmFile.functionTypeIndexes[index]], m_wasmFile.codeBlocks[index], args);
}

std::vector<Value> VM::run_function_new_parser(const FunctionType& functionType, const Code& code, const std::vector<Value>& args)
{
    m_frame_stack.push(m_frame);
    m_frame = new Frame();

    for (uint32_t i = 0; i < args.size(); i++)
    {
        m_frame->locals.push_back(args.at(i));
    }

    for (const auto& local : code.locals)
    {
        for (uint32_t i = 0; i < local.count; i++)
        {
            m_frame->locals.push_back(default_value_for_type(local.type));
        }
    }

    m_frame->stack.push(Label {
        .continuation = (uint32_t)code.instructions.size(),
        .arity = (uint32_t)functionType.returns.size(),
        .beginType = LabelBeginType::Other
    });

    while (m_frame->ip < code.instructions.size())
    {
        const Instruction& instruction = code.instructions[m_frame->ip++];

        switch (instruction.opcode)
        {
            case Opcode::unreachable:
                throw Trap();
            case Opcode::nop:
                break;
            case Opcode::block:
            case Opcode::loop: {
                const BlockLoopArguments& arguments = std::get<BlockLoopArguments>(instruction.arguments);
                std::vector<Value> params = m_frame->stack.pop_n_values(arguments.blockType.get_param_types(m_wasmFile).size());
                m_frame->stack.push(arguments.label);
                m_frame->stack.push_values(params);
                break;
            }
            case Opcode::if_: {
                const IfArguments& arguments = std::get<IfArguments>(instruction.arguments);

                uint32_t value = m_frame->stack.pop_as<uint32_t>();
                std::vector<Value> params = m_frame->stack.pop_n_values(arguments.blockType.get_param_types(m_wasmFile).size());

                if (arguments.elseLocation.has_value())
                {
                    if (value == 0)
                        m_frame->ip = arguments.elseLocation.value() + 1;
                    m_frame->stack.push(arguments.endLabel);
                }
                else 
                {
                    if (value != 0)
                        m_frame->stack.push(arguments.endLabel);
                    else
                        m_frame->ip = arguments.endLabel.continuation;

                }

                m_frame->stack.push_values(params);
                break;
            }
            case Opcode::else_:
                m_frame->ip = std::get<Label>(instruction.arguments).continuation;
                [[fallthrough]];
            case Opcode::end: {
                std::vector<Value> values;
                while (!std::holds_alternative<Label>(m_frame->stack.peek()))
                {
                    values.push_back(m_frame->stack.pop());
                }
                m_frame->stack.pop_as<Label>();

                while (!values.empty())
                {
                    m_frame->stack.push(values.back());
                    values.pop_back();
                }
                break;
            }
            case Opcode::br:
                branch_to_label(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::br_if:
                if (m_frame->stack.pop_as<uint32_t>() != 0)
                    branch_to_label(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::br_table: {
                const BranchTableArguments& arguments = std::get<BranchTableArguments>(instruction.arguments);
                uint32_t i = m_frame->stack.pop_as<uint32_t>();
                if (i < arguments.labels.size())
                    branch_to_label(arguments.labels[i]);
                else
                    branch_to_label(arguments.defaultLabel);
                break;
            }
            case Opcode::return_: {
                std::vector<Value> returnValues;
                for (size_t i = 0; i < functionType.returns.size(); i++)
                    returnValues.push_back(m_frame->stack.pop());

                std::reverse(returnValues.begin(), returnValues.end());

                for (size_t i = 0; i < returnValues.size(); i++)
                    if (get_value_type(returnValues[i]) != functionType.returns[i])
                        throw Trap();

                clean_up_frame();

                return returnValues;
            }
            case Opcode::call:
                call_function(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::call_indirect: {
                const CallIndirectArguments& arguments = std::get<CallIndirectArguments>(instruction.arguments);

                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                if (index >= m_tables[arguments.tableIndex].size())
                    throw Trap();

                if (m_tables[arguments.tableIndex][index] == UINT32_MAX)
                    throw Trap();

                if (m_wasmFile.functionTypes[m_wasmFile.functionTypeIndexes[m_tables[arguments.tableIndex][index]]] != m_wasmFile.functionTypes[arguments.typeIndex])
                    throw Trap();

                call_function(m_tables[arguments.tableIndex][index]);
                break;
            }
            case Opcode::drop:
                m_frame->stack.pop();
                break;
            case Opcode::select_: {
                uint32_t value = m_frame->stack.pop_as<uint32_t>();

                Value val2 = m_frame->stack.pop();
                Value val1 = m_frame->stack.pop();

                m_frame->stack.push(value != 0 ? val1 : val2);
                break;
            }
            case Opcode::local_get:
                m_frame->stack.push(m_frame->locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::local_set:
                m_frame->locals[std::get<uint32_t>(instruction.arguments)] = m_frame->stack.pop();
                break;
            case Opcode::local_tee:
                m_frame->locals[std::get<uint32_t>(instruction.arguments)] = m_frame->stack.peek();
                break;
            case Opcode::global_get:
                m_frame->stack.push(m_globals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::global_set:
                m_globals[std::get<uint32_t>(instruction.arguments)] = m_frame->stack.pop();
                break;
            case Opcode::i32_load:
                run_load_instruction<uint32_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load:
                run_load_instruction<uint64_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::f32_load:
                run_load_instruction<float, float>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::f64_load:
                run_load_instruction<double, double>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load8_s:
                run_load_instruction<int8_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load8_u:
                run_load_instruction<uint8_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load16_s:
                run_load_instruction<int16_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load16_u:
                run_load_instruction<uint16_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load8_s:
                run_load_instruction<int8_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load8_u:
                run_load_instruction<uint8_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load16_s:
                run_load_instruction<int16_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load16_u:
                run_load_instruction<uint16_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load32_s:
                run_load_instruction<int32_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load32_u:
                run_load_instruction<uint32_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store:
                run_store_instruction<uint32_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store:
                run_store_instruction<uint64_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::f32_store:
                run_store_instruction<float, float>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::f64_store:
                run_store_instruction<double, double>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store8:
                run_store_instruction<uint8_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store16:
                run_store_instruction<uint16_t, uint32_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store8:
                run_store_instruction<uint8_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store16:
                run_store_instruction<uint16_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store32:
                run_store_instruction<uint32_t, uint64_t>(std::get<MemArg>(instruction.arguments));
                break;
            case Opcode::memory_size:
                m_frame->stack.push(m_memory_size);
                break;
            case Opcode::memory_grow: {
                uint32_t addPages = m_frame->stack.pop_as<uint32_t>();
                m_frame->stack.push(m_memory_size);

                if (m_memory_size + addPages <= m_wasmFile.memories[0].limits.max)
                {
                    uint8_t* newMemory = (uint8_t*)malloc((m_memory_size + addPages) * WASM_PAGE_SIZE);
                    memcpy(newMemory, m_memory, std::min(m_memory_size * WASM_PAGE_SIZE, (m_memory_size + addPages) * WASM_PAGE_SIZE));
                    free(m_memory);
                    m_memory = newMemory;
                    m_memory_size += addPages;
                }
                break;
            }
            case Opcode::i32_const:
                m_frame->stack.push(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::i64_const:
                m_frame->stack.push(std::get<uint64_t>(instruction.arguments));
                break;
            case Opcode::f32_const:
                m_frame->stack.push(std::get<float>(instruction.arguments));
                break;
            case Opcode::f64_const:
                m_frame->stack.push(std::get<double>(instruction.arguments));
                break;
            case Opcode::i32_eqz:
                run_unary_operation<uint32_t, operation_eqz>();
                break;
            case Opcode::i32_eq:
                run_binary_operation<uint32_t, int32_t, operation_eq>();
                break;
            case Opcode::i32_ne:
                run_binary_operation<uint32_t, int32_t, operation_ne>();
                break;
            case Opcode::i32_lt_s:
                run_binary_operation<int32_t, int32_t, operation_lt>();
                break;
            case Opcode::i32_lt_u:
                run_binary_operation<uint32_t, int32_t, operation_lt>();
                break;
            case Opcode::i32_gt_s:
                run_binary_operation<int32_t, int32_t, operation_gt>();
                break;
            case Opcode::i32_gt_u:
                run_binary_operation<uint32_t, int32_t, operation_gt>();
                break;
            case Opcode::i32_le_s:
                run_binary_operation<int32_t, int32_t, operation_le>();
                break;
            case Opcode::i32_le_u:
                run_binary_operation<uint32_t, int32_t, operation_le>();
                break;
            case Opcode::i32_ge_s:
                run_binary_operation<int32_t, int32_t, operation_ge>();
                break;
            case Opcode::i32_ge_u:
                run_binary_operation<uint32_t, int32_t, operation_ge>();
                break;
            case Opcode::i64_eqz:
                run_unary_operation<uint64_t, operation_eqz>();
                break;
            case Opcode::i64_eq:
                run_binary_operation<uint64_t, int64_t, operation_eq>();
                break;
            case Opcode::i64_ne:
                run_binary_operation<uint64_t, int64_t, operation_ne>();
                break;
            case Opcode::i64_lt_s:
                run_binary_operation<int64_t, int64_t, operation_lt>();
                break;
            case Opcode::i64_lt_u:
                run_binary_operation<uint64_t, int64_t, operation_lt>();
                break;
            case Opcode::i64_gt_s:
                run_binary_operation<int64_t, int64_t, operation_gt>();
                break;
            case Opcode::i64_gt_u:
                run_binary_operation<uint64_t, int64_t, operation_gt>();
                break;
            case Opcode::i64_le_s:
                run_binary_operation<int64_t, int64_t, operation_le>();
                break;
            case Opcode::i64_le_u:
                run_binary_operation<uint64_t, int64_t, operation_le>();
                break;
            case Opcode::i64_ge_s:
                run_binary_operation<int64_t, int64_t, operation_ge>();
                break;
            case Opcode::i64_ge_u:
                run_binary_operation<uint64_t, int64_t, operation_ge>();
                break;
            case Opcode::f32_eq:
                run_binary_operation<float, float, operation_eq>();
                break;
            case Opcode::f32_ne:
                run_binary_operation<float, float, operation_ne>();
                break;
            case Opcode::f32_lt:
                run_binary_operation<float, float, operation_lt>();
                break;
            case Opcode::f32_gt:
                run_binary_operation<float, float, operation_gt>();
                break;
            case Opcode::f32_le:
                run_binary_operation<float, float, operation_le>();
                break;
            case Opcode::f32_ge:
                run_binary_operation<float, float, operation_ge>();
                break;
            case Opcode::f64_eq:
                run_binary_operation<double, double, operation_eq>();
                break;
            case Opcode::f64_ne:
                run_binary_operation<double, double, operation_ne>();
                break;
            case Opcode::f64_lt:
                run_binary_operation<double, double, operation_lt>();
                break;
            case Opcode::f64_gt:
                run_binary_operation<double, double, operation_gt>();
                break;
            case Opcode::f64_le:
                run_binary_operation<double, double, operation_le>();
                break;
            case Opcode::f64_ge:
                run_binary_operation<double, double, operation_ge>();
                break;
            case Opcode::i32_clz:
                run_unary_operation<uint32_t, operation_clz>();
                break;
            case Opcode::i32_ctz:
                run_unary_operation<uint32_t, operation_ctz>();
                break;
            case Opcode::i32_popcnt:
                run_unary_operation<uint32_t, operation_popcnt>();
                break;
            case Opcode::i32_add:
                run_binary_operation<uint32_t, int32_t, operation_add>();
                break;
            case Opcode::i32_sub:
                run_binary_operation<uint32_t, int32_t, operation_sub>();
                break;
            case Opcode::i32_mul:
                run_binary_operation<uint32_t, int32_t, operation_mul>();
                break;
            case Opcode::i32_div_s:
                run_binary_operation<int32_t, int32_t, operation_div>();
                break;
            case Opcode::i32_div_u:
                run_binary_operation<uint32_t, int32_t, operation_div>();
                break;
            case Opcode::i32_rem_s:
                run_binary_operation<int32_t, int32_t, operation_rem>();
                break;
            case Opcode::i32_rem_u:
                run_binary_operation<uint32_t, int32_t, operation_rem>();
                break;
            case Opcode::i32_and:
                run_binary_operation<uint32_t, int32_t, operation_and>();
                break;
            case Opcode::i32_or:
                run_binary_operation<uint32_t, int32_t, operation_or>();
                break;
            case Opcode::i32_xor:
                run_binary_operation<uint32_t, int32_t, operation_xor>();
                break;
            case Opcode::i32_shl:
                run_binary_operation<uint32_t, int32_t, operation_shl>();
                break;
            case Opcode::i32_shr_s:
                run_binary_operation<int32_t, int32_t, operation_shr>();
                break;
            case Opcode::i32_shr_u:
                run_binary_operation<uint32_t, int32_t, operation_shr>();
                break;
            case Opcode::i32_rotl:
                run_binary_operation<uint32_t, int32_t, operation_rotl>();
                break;
            case Opcode::i32_rotr:
                run_binary_operation<uint32_t, int32_t, operation_rotr>();
                break;
            case Opcode::i64_clz:
                run_unary_operation<uint64_t, operation_clz>();
                break;
            case Opcode::i64_ctz:
                run_unary_operation<uint64_t, operation_ctz>();
                break;
            case Opcode::i64_popcnt:
                run_unary_operation<uint64_t, operation_popcnt>();
                break;
            case Opcode::i64_add:
                run_binary_operation<uint64_t, int64_t, operation_add>();
                break;
            case Opcode::i64_sub:
                run_binary_operation<uint64_t, int64_t, operation_sub>();
                break;
            case Opcode::i64_mul:
                run_binary_operation<uint64_t, int64_t, operation_mul>();
                break;
            case Opcode::i64_div_s:
                run_binary_operation<int64_t, int64_t, operation_div>();
                break;
            case Opcode::i64_div_u:
                run_binary_operation<uint64_t, int64_t, operation_div>();
                break;
            case Opcode::i64_rem_s:
                run_binary_operation<int64_t, int64_t, operation_rem>();
                break;
            case Opcode::i64_rem_u:
                run_binary_operation<uint64_t, int64_t, operation_rem>();
                break;
            case Opcode::i64_and:
                run_binary_operation<uint64_t, int64_t, operation_and>();
                break;
            case Opcode::i64_or:
                run_binary_operation<uint64_t, int64_t, operation_or>();
                break;
            case Opcode::i64_xor:
                run_binary_operation<uint64_t, int64_t, operation_xor>();
                break;
            case Opcode::i64_shl:
                run_binary_operation<uint64_t, int64_t, operation_shl>();
                break;
            case Opcode::i64_shr_s:
                run_binary_operation<int64_t, int64_t, operation_shr>();
                break;
            case Opcode::i64_shr_u:
                run_binary_operation<uint64_t, int64_t, operation_shr>();
                break;
            case Opcode::i64_rotl:
                run_binary_operation<uint64_t, uint64_t, operation_rotl>();
                break;
            case Opcode::i64_rotr:
                run_binary_operation<uint64_t, uint64_t, operation_rotr>();
                break;
            case Opcode::f32_abs:
                run_unary_operation<float, operation_abs>();
                break;
            case Opcode::f32_neg:
                run_unary_operation<float, operation_neg>();
                break;
            case Opcode::f32_ceil:
                run_unary_operation<float, operation_ceil>();
                break;
            case Opcode::f32_floor:
                run_unary_operation<float, operation_floor>();
                break;
            case Opcode::f32_trunc:
                run_unary_operation<float, operation_trunc>();
                break;
            case Opcode::f32_nearest:
                run_unary_operation<float, operation_nearest>();
                break;
            case Opcode::f32_sqrt:
                run_unary_operation<float, operation_sqrt>();
                break;
            case Opcode::f32_add:
                run_binary_operation<float, float, operation_add>();
                break;
            case Opcode::f32_sub:
                run_binary_operation<float, float, operation_sub>();
                break;
            case Opcode::f32_mul:
                run_binary_operation<float, float, operation_mul>();
                break;
            case Opcode::f32_div:
                run_binary_operation<float, float, operation_div>();
                break;
            case Opcode::f32_min:
                run_binary_operation<float, float, operation_min>();
                break;
            case Opcode::f32_max:
                run_binary_operation<float, float, operation_max>();
                break;
            case Opcode::f32_copysign:
                run_binary_operation<float, float, operation_copysign>();
                break;
            case Opcode::f64_abs:
                run_unary_operation<double, operation_abs>();
                break;
            case Opcode::f64_neg:
                run_unary_operation<double, operation_neg>();
                break;
            case Opcode::f64_ceil:
                run_unary_operation<double, operation_ceil>();
                break;
            case Opcode::f64_floor:
                run_unary_operation<double, operation_floor>();
                break;
            case Opcode::f64_trunc:
                run_unary_operation<double, operation_trunc>();
                break;
            case Opcode::f64_nearest:
                run_unary_operation<double, operation_nearest>();
                break;
            case Opcode::f64_sqrt:
                run_unary_operation<double, operation_sqrt>();
                break;
            case Opcode::f64_add:
                run_binary_operation<double, double, operation_add>();
                break;
            case Opcode::f64_sub:
                run_binary_operation<double, double, operation_sub>();
                break;
            case Opcode::f64_mul:
                run_binary_operation<double, double, operation_mul>();
                break;
            case Opcode::f64_div:
                run_binary_operation<double, double, operation_div>();
                break;
            case Opcode::f64_min:
                run_binary_operation<double, double, operation_min>();
                break;
            case Opcode::f64_max:
                run_binary_operation<double, double, operation_max>();
                break;
            case Opcode::f64_copysign:
                run_binary_operation<double, double, operation_copysign>();
                break;
            case Opcode::i32_wrap_i64:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::i32_trunc_f32_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<float>());
                break;
            case Opcode::i32_trunc_f32_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<float>());
                break;
            case i64_extend_i32_s:
                m_frame->stack.push((uint64_t)(int64_t)(int32_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case i64_extend_i32_u:
                m_frame->stack.push((uint64_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::i32_trunc_f64_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<double>());
                break;
            case Opcode::i32_trunc_f64_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<double>());
                break;
            case Opcode::i64_trunc_f32_s:
                m_frame->stack.push((uint64_t)(int64_t)m_frame->stack.pop_as<float>());
                break;
            case Opcode::i64_trunc_f32_u:
                m_frame->stack.push((uint64_t)m_frame->stack.pop_as<float>());
                break;
            case Opcode::i64_trunc_f64_s:
                m_frame->stack.push((uint64_t)(int64_t)m_frame->stack.pop_as<double>());
                break;
            case Opcode::i64_trunc_f64_u:
                m_frame->stack.push((uint64_t)m_frame->stack.pop_as<double>());
                break;
            case Opcode::f32_convert_i32_s:
                m_frame->stack.push((float)(int32_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::f32_convert_i32_u:
                m_frame->stack.push((float)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::f32_convert_i64_s:
                m_frame->stack.push((float)(int64_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::f32_convert_i64_u:
                m_frame->stack.push((float)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::f32_demote_f64:
                m_frame->stack.push((float)m_frame->stack.pop_as<double>());
                break;
            case Opcode::f64_convert_i32_s:
                m_frame->stack.push((double)(int32_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::f64_convert_i32_u:
                m_frame->stack.push((double)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::f64_convert_i64_s:
                m_frame->stack.push((double)(int64_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::f64_convert_i64_u:
                m_frame->stack.push((double)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::f64_promote_f32:
                m_frame->stack.push((double)m_frame->stack.pop_as<float>());
                break;
            case Opcode::i32_reinterpret_f32: {
                float value = m_frame->stack.pop_as<float>();
                m_frame->stack.push(*(uint32_t*)&value);
                break;
            }
            case Opcode::i64_reinterpret_f64: {
                double value = m_frame->stack.pop_as<double>();
                m_frame->stack.push(*(uint64_t*)&value);
                break;
            }
            case Opcode::f32_reinterpret_i32: {
                uint32_t value = m_frame->stack.pop_as<uint32_t>();
                m_frame->stack.push(*(float*)&value);
                break;
            }
            case Opcode::f64_reinterpret_i64: {
                uint64_t value = m_frame->stack.pop_as<uint64_t>();
                m_frame->stack.push(*(double*)&value);
                break;
            }
            case Opcode::i32_extend8_s:
                m_frame->stack.push((uint32_t)(int32_t)(int8_t)(uint8_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::i32_extend16_s:
                m_frame->stack.push((uint32_t)(int32_t)(int16_t)(uint16_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::i64_extend8_s:
                m_frame->stack.push((uint64_t)(int64_t)(int8_t)(uint8_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::i64_extend16_s:
                m_frame->stack.push((uint64_t)(int64_t)(int16_t)(uint16_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::i64_extend32_s:
                m_frame->stack.push((uint64_t)(int64_t)(int32_t)(uint32_t)m_frame->stack.pop_as<uint64_t>());
                break;
            case Opcode::ref_null:
                m_frame->stack.push(default_value_for_type(std::get<Type>(instruction.arguments)));
                break;
            case Opcode::ref_is_null:
                m_frame->stack.push((uint32_t)(m_frame->stack.pop_as<FunctionRefrence>().functionIndex == UINT32_MAX));
                break;
            case Opcode::ref_func:
                m_frame->stack.push((FunctionRefrence) { std::get<uint32_t>(instruction.arguments) });
                break;
            case Opcode::memory_copy: {
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)source + count > m_memory_size * WASM_PAGE_SIZE || (uint64_t)destination + count > m_memory_size * WASM_PAGE_SIZE)
                    throw Trap();

                memcpy(m_memory + destination, m_memory + source, count);
                break;
            }
            case Opcode::memory_fill: {
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t val = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)destination + count > m_memory_size * WASM_PAGE_SIZE)
                    throw Trap();
                
                memset(m_memory + destination, val, count);
                break;
            }
            default:
                fprintf(stderr, "Error: Unknown opcode 0x%x\n", instruction.opcode);
                throw Trap();
        }
    }

    std::vector<Value> returnValues;
    for (size_t i = 0; i < functionType.returns.size(); i++)
    {
        // FIXME: Check the type
        returnValues.push_back(m_frame->stack.pop());
    }

    std::reverse(returnValues.begin(), returnValues.end());

    if (m_frame->stack.size() != 0)
    {
        printf("Error: Stack not empty when running function, size is %zu\n", m_frame->stack.size());
        throw Trap();
    }

    clean_up_frame();

    return returnValues;
}

Value VM::run_bare_code_returning(std::vector<Instruction> instructions, Type returnType)
{
    // clang-format off
    std::vector<Value> values = run_function_new_parser({
            .params = {},
            .returns = {(uint8_t)returnType},
        },
        {
            .locals = {},
            .instructions = instructions,
        },
        {});
    // clang-format on

    assert(values.size() == 1);
    assert(!std::holds_alternative<Label>(values.at(0)));

    return values.at(0);
}

void VM::clean_up_frame()
{
    delete m_frame;
    m_frame = m_frame_stack.top();
    m_frame_stack.pop();
}

template <typename LhsType, typename RhsType, Value(function)(LhsType, RhsType)>
void VM::run_binary_operation()
{
    RhsType rhs = m_frame->stack.pop_as<RhsType>();
    LhsType lhs = m_frame->stack.pop_as<LhsType>();
    m_frame->stack.push(function(lhs, rhs));
}

template <typename T, Value(function)(T)>
void VM::run_unary_operation()
{
    T a = m_frame->stack.pop_as<T>();
    m_frame->stack.push(function(a));
}

template <typename ActualType, typename StackType>
void VM::run_load_instruction(const MemArg& memArg)
{
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > m_memory_size * WASM_PAGE_SIZE)
        throw Trap();

    ActualType value;
    memcpy(&value, &m_memory[address + memArg.offset], sizeof(ActualType));

    m_frame->stack.push((StackType)value);
}

template <typename ActualType, typename StackType>
void VM::run_store_instruction(const MemArg& memArg)
{
    ActualType value = (ActualType)m_frame->stack.pop_as<StackType>();
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > m_memory_size * WASM_PAGE_SIZE)
        throw Trap();

    memcpy(&m_memory[address + memArg.offset], &value, sizeof(ActualType));
}

void VM::branch_to_label(uint32_t index)
{
    Label label = m_frame->stack.nth_label(index);

    // FIXME: Actually verify the values
    // std::vector<Value> values = (label.beginType == LabelBeginType::LoopInvalid ? (std::vector<Value>){} : m_frame->stack.pop_n_values(label.arity));
    std::vector<Value> values = m_frame->stack.pop_n_values(label.arity);

    Value value;
    for (uint32_t drop_count = index + 1; drop_count > 0;)
    {
        value = m_frame->stack.pop();
        if (std::holds_alternative<Label>(value))
            drop_count--;
    }

    m_frame->stack.push_values(values);

    m_frame->ip = std::get<Label>(value).continuation;
}

void VM::call_function(uint32_t index)
{
    FunctionType functionType = m_wasmFile.functionTypes[index < m_wasmFile.imports.size() ? m_wasmFile.imports.at(index).index : m_wasmFile.functionTypeIndexes[index]];

    std::vector<Value> args;
    for (size_t i = 0; i < functionType.params.size(); i++)
    {
        args.push_back(m_frame->stack.pop());
    }
    std::reverse(args.begin(), args.end());

    for (size_t i = 0; i < functionType.params.size(); i++)
    {
        if (get_value_type(args.at(i)) != functionType.params.at(i))
            throw Trap();
    }

    std::vector<Value> returnedValues;
    if (index < m_wasmFile.imports.size())
    {
        const auto& import = m_wasmFile.imports.at(index);
        if (import.environment == "wasi_snapshot_preview1")
        {
            returnedValues = run_wasi_call(import.name, args);
        }
        else
        {
            fprintf(stderr, "Error: Invalid import enviroment: %s\n", import.environment.c_str());
            throw Trap();
        }
    }
    else
    {
        returnedValues = run_function(index, args);
    }

    for (const auto& returned : returnedValues)
        m_frame->stack.push(returned);
}

std::vector<Value> VM::run_wasi_call(const std::string& name, const std::vector<Value>& args)
{
    if (name == "clock_time_get")
    {
        /*
        enum class ClockID : u32 {
            Realtime,
            Monotonic,
            ProcessCPUTimeID,
            ThreadCPUTimeID,
        };
        */

        if (args.size() != 3)
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[0]))
            throw Trap();

        if (!std::holds_alternative<uint64_t>(args[1]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[2]))
            throw Trap();

        assert(std::get<uint32_t>(args[0]) == 0);
        assert(std::get<uint64_t>(args[1]) == 1000);

        struct timespec ts;
        assert(clock_gettime(0, &ts) == 0);
        uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
        memcpy(m_memory + std::get<uint32_t>(args[2]), &nanos, sizeof(nanos));

        return { (uint32_t)0 };
    }
    else if (name == "fd_fdstat_get")
    {
        if (args.size() != 2)
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[0]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[1]))
            throw Trap();

        struct stat statBuffer;
        fstat(std::get<uint32_t>(args[0]), &statBuffer);

        WASI::FDStat fdStat {
            .fs_filetype = WASI::file_type_from_stat(statBuffer),
            .fs_flags = { 0 },
            .fs_rights_base = { 0 },
            .fs_rights_inheriting = { 0 },
        };
        memcpy(m_memory + std::get<uint32_t>(args[1]), &fdStat, sizeof(fdStat));

        return { (uint32_t)0 };
    }
    else if (name == "fd_write")
    {
        if (args.size() != 4)
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[0]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[1]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[2]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[3]))
            throw Trap();

        uint32_t written = 0;

        WASI::IOVector* iov = (WASI::IOVector*)(m_memory + std::get<uint32_t>(args[1]));

        for (uint32_t i = 0; i < std::get<uint32_t>(args[2]); i++)
            written += (uint32_t)write(std::get<uint32_t>(args[0]), (m_memory + iov[i].pointer), iov[i].length);
        
        memcpy(m_memory + std::get<uint32_t>(args[3]), &written, sizeof(written));

        return { written };
    }
    else if (name == "random_get")
    {
        if (args.size() != 2)
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[0]))
            throw Trap();

        if (!std::holds_alternative<uint32_t>(args[1]))
            throw Trap();

        arc4random_buf(m_memory + std::get<uint32_t>(args[0]), std::get<uint32_t>(args[1]));

        return { (uint32_t)0 };
    }
    else
    {
        fprintf(stderr, "Error: Invalid WASI call: %s\n", name.c_str());
        throw Trap();
    }
}
