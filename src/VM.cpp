#include <MemoryStream.h>
#include <Opcode.h>
#include <Operators.h>
#include <Type.h>
#include <VM.h>
#include <WASI.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>
#include <Util.h>

void VM::load_module(WasmFile& file)
{
    m_current_module = new Module();
    m_current_module->wasmFile = file;

    for (const auto& [index, global] : file.globals)
    {
        // FIXME: verify type
        m_current_module->globals[index] = run_bare_code_returning(m_current_module, global.initCode, (Type)global.type);
    }

    if (file.memories.size() > 0)
    {
        m_current_module->memory.size = file.memories.at(0).limits.min;
        m_current_module->memory.max = file.memories.at(0).limits.max;
        m_current_module->memory.data = (uint8_t*)malloc(m_current_module->memory.size * WASM_PAGE_SIZE);
        memset(m_current_module->memory.data, 0, m_current_module->memory.size * WASM_PAGE_SIZE);
    }

    for (const auto& data : file.dataBlocks)
    {
        if (data.type == 0)
        {
            Value beginValue = run_bare_code_returning(m_current_module, data.expr, i32);
            assert(std::holds_alternative<uint32_t>(beginValue));
            uint32_t begin = std::get<uint32_t>(beginValue);

            memcpy(get_memory(m_current_module).data + begin, data.data.data(), data.data.size());
        }
    }

    for (const auto& [index, table] : file.tables)
    {
        assert(table.limits.min <= table.limits.max);
        std::vector<Reference> tableVector;
        tableVector.reserve(table.limits.min);
        for (uint32_t i = 0; i < table.limits.min; i++)
            tableVector.push_back(Reference { get_reference_type_from_reftype(table.refType), UINT32_MAX });
        m_current_module->tables.push_back(tableVector);
    }

    for (const auto& element : file.elements)
    {
        if (element.mode == ElementMode::Active)
        {
            Value beginValue = run_bare_code_returning(m_current_module, element.expr, i32);
            assert(std::holds_alternative<uint32_t>(beginValue));
            uint32_t begin = std::get<uint32_t>(beginValue);
            for (size_t i = 0; i < element.functionIndexes.size(); i++)
            {
                get_table(element.table, m_current_module)[begin + i] = Reference { ReferenceType::Function, element.functionIndexes[i] };
            }
        }
    }

    if (m_current_module->wasmFile.startFunction != UINT32_MAX)
        run_function(m_current_module, m_current_module->wasmFile.startFunction, {});
}

void VM::register_module(const std::string& name)
{
    m_registered_modules[name] = m_current_module;
}

std::vector<Value> VM::run_function(const std::string& name, const std::vector<Value>& args)
{
    return run_function(m_current_module, name, args);
}

std::vector<Value> VM::run_function(const std::string& mod, const std::string& name, const std::vector<Value>& args)
{
    return run_function(m_registered_modules[mod], name, args);
}

std::vector<Value> VM::run_function(Module* mod, const std::string& name, const std::vector<Value>& args)
{
    Export functionExport = mod->wasmFile.find_export_by_name(name);
    assert(functionExport.type == 0);
    return run_function(mod, functionExport.index, args);
}

std::vector<Value> VM::run_function(Module* mod, uint32_t index, const std::vector<Value>& args)
{
    return run_function(mod, mod->wasmFile.functionTypes[mod->wasmFile.functionTypeIndexes[index]], mod->wasmFile.codeBlocks[index], args);
}

std::vector<Value> VM::run_function(Module* mod, const FunctionType& functionType, const Code& code, const std::vector<Value>& args)
{
    m_frame_stack.push(m_frame);
    m_frame = new Frame(mod);

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
                std::vector<Value> params = m_frame->stack.pop_n_values(arguments.blockType.get_param_types(mod->wasmFile).size());
                m_frame->stack.push(arguments.label);
                m_frame->stack.push_values(params);
                break;
            }
            case Opcode::if_: {
                const IfArguments& arguments = std::get<IfArguments>(instruction.arguments);

                uint32_t value = m_frame->stack.pop_as<uint32_t>();
                std::vector<Value> params = m_frame->stack.pop_n_values(arguments.blockType.get_param_types(mod->wasmFile).size());

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

                auto& table = get_table(arguments.tableIndex, mod);

                if (index >= table.size())
                    throw Trap();

                Reference reference = table[index];

                if (reference.index == UINT32_MAX)
                    throw Trap();

                if (mod->wasmFile.functionTypes[mod->wasmFile.functionTypeIndexes[reference.index]] != mod->wasmFile.functionTypes[arguments.typeIndex])
                    throw Trap();

                call_function(reference.index);
                break;
            }
            case Opcode::drop:
                m_frame->stack.pop();
                break;
            case Opcode::select_typed:
                // FIXME: Validate the types
                [[fallthrough]];
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
                m_frame->stack.push(get_global(std::get<uint32_t>(instruction.arguments), mod));
                break;
            case Opcode::global_set:
                get_global(std::get<uint32_t>(instruction.arguments), mod) = m_frame->stack.pop();
                break;
            case Opcode::table_get: {
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                if (index >= mod->tables[std::get<uint32_t>(instruction.arguments)].size())
                    throw Trap();
                m_frame->stack.push(mod->tables[std::get<uint32_t>(instruction.arguments)][index]);
                break;
            }
            case Opcode::table_set: {
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                if (index >= mod->tables[std::get<uint32_t>(instruction.arguments)].size())
                    throw Trap();
                mod->tables[std::get<uint32_t>(instruction.arguments)][index] = value;
                break;
            }
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
                m_frame->stack.push(get_memory(mod).size);
                break;
            case Opcode::memory_grow: {
                auto& memory = get_memory(mod);

                uint32_t addPages = m_frame->stack.pop_as<uint32_t>();

                if (memory.size + addPages > std::min(memory.max, 65536u))
                {
                    m_frame->stack.push((uint32_t)-1);
                    break;
                }

                m_frame->stack.push(memory.size);
                uint32_t newSize = (memory.size + addPages) * WASM_PAGE_SIZE;
                uint8_t* newMemory = (uint8_t*)malloc(newSize);
                memset(newMemory, 0, newSize);
                memcpy(newMemory, memory.data, std::min(memory.size * WASM_PAGE_SIZE, newSize));
                free(memory.data);
                memory.data = newMemory;
                memory.size += addPages;
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
                run_unary_operation<float, operation_trunc<int32_t>>();
                break;
            case Opcode::i32_trunc_f32_u:
                run_unary_operation<float, operation_trunc<uint32_t>>();
                break;
            case Opcode::i32_trunc_f64_s:
                run_unary_operation<double, operation_trunc<int32_t>>();
                break;
            case Opcode::i32_trunc_f64_u:
                run_unary_operation<double, operation_trunc<uint32_t>>();
                break;
            case i64_extend_i32_s:
                m_frame->stack.push((uint64_t)(int64_t)(int32_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case i64_extend_i32_u:
                m_frame->stack.push((uint64_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::i64_trunc_f32_s:
                run_unary_operation<float, operation_trunc<int64_t>>();
                break;
            case Opcode::i64_trunc_f32_u:
                run_unary_operation<float, operation_trunc<uint64_t>>();
                break;
            case Opcode::i64_trunc_f64_s:
                run_unary_operation<double, operation_trunc<int64_t>>();
                break;
            case Opcode::i64_trunc_f64_u:
                run_unary_operation<double, operation_trunc<uint64_t>>();
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
                m_frame->stack.push((uint32_t)(m_frame->stack.pop_as<Reference>().index == UINT32_MAX));
                break;
            case Opcode::ref_func:
                m_frame->stack.push(Reference { ReferenceType::Function, std::get<uint32_t>(instruction.arguments) });
                break;
            case Opcode::i32_trunc_sat_f32_s:
                run_unary_operation<float, operation_trunc_sat<int32_t>>();
                break;
            case Opcode::i32_trunc_sat_f32_u:
                run_unary_operation<float, operation_trunc_sat<uint32_t>>();
                break;
            case Opcode::i32_trunc_sat_f64_s:
                run_unary_operation<double, operation_trunc_sat<int32_t>>();
                break;
            case Opcode::i32_trunc_sat_f64_u:
                run_unary_operation<double, operation_trunc_sat<uint32_t>>();
                break;
            case Opcode::i64_trunc_sat_f32_s:
                run_unary_operation<float, operation_trunc_sat<int64_t>>();
                break;
            case Opcode::i64_trunc_sat_f32_u:
                run_unary_operation<float, operation_trunc_sat<uint64_t>>();
                break;
            case Opcode::i64_trunc_sat_f64_s:
                run_unary_operation<double, operation_trunc_sat<int64_t>>();
                break;
            case Opcode::i64_trunc_sat_f64_u:
                run_unary_operation<double, operation_trunc_sat<uint64_t>>();
                break;
            case Opcode::memory_init: {
                auto& memory = get_memory(mod);

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                const Data& data = mod->wasmFile.dataBlocks[std::get<uint32_t>(instruction.arguments)];

                if ((uint64_t)source + count > data.data.size())
                    throw Trap();

                if ((uint64_t)destination + count > memory.size * WASM_PAGE_SIZE)
                    throw Trap();

                memcpy(memory.data + destination, data.data.data() + source, count);
                break;
            }
            case Opcode::data_drop: {
                Data& data = mod->wasmFile.dataBlocks[std::get<uint32_t>(instruction.arguments)];
                data.type = UINT32_MAX;
                data.expr.clear();
                data.data.clear();
                break;
            }
            case Opcode::memory_copy: {
                auto& memory = get_memory(mod);

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)source + count > memory.size * WASM_PAGE_SIZE || (uint64_t)destination + count > memory.size * WASM_PAGE_SIZE)
                    throw Trap();

                memcpy(memory.data + destination, memory.data + source, count);
                break;
            }
            case Opcode::memory_fill: {
                auto& memory = get_memory(mod);

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t val = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)destination + count > memory.size * WASM_PAGE_SIZE)
                    throw Trap();
                
                memset(memory.data + destination, val, count);
                break;
            }
            case Opcode::table_init: {
                const TableInitArguments& arguments = std::get<TableInitArguments>(instruction.arguments);
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto& table = get_table(arguments.tableIndex, mod);

                if ((uint64_t)source + count > mod->wasmFile.elements[arguments.elementIndex].functionIndexes.size() || (uint64_t)destination + count > table.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                    table[destination + i] = Reference { ReferenceType::Function, mod->wasmFile.elements[arguments.elementIndex].functionIndexes[source + i] };
                break;
            }
            case Opcode::elem_drop: {
                Element& elem = mod->wasmFile.elements[std::get<uint32_t>(instruction.arguments)];
                elem.type = UINT32_MAX;
                elem.expr.clear();
                elem.functionIndexes.clear();
                elem.table = UINT32_MAX;
                break;
            }
            case Opcode::table_copy: {
                const TableCopyArguments& arguments = std::get<TableCopyArguments>(instruction.arguments);
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto& destinationTable = get_table(arguments.destination, mod);
                auto& sourceTable = get_table(arguments.source, mod);

                if ((uint64_t)source + count > sourceTable.size() || (uint64_t)destination + count > destinationTable.size())
                    throw Trap();

                if (count == 0)
                    break;

                if (destination <= source)
                {
                    for (uint32_t i = 0; i < count; i++)
                        destinationTable[destination + i] = sourceTable[source + i];
                }
                else
                {
                    for (int64_t i = count - 1; i > -1; i--)
                        destinationTable[destination + i] = sourceTable[source + i];
                }
                break;
            }
            case Opcode::table_grow: {
                uint32_t addEntries = m_frame->stack.pop_as<uint32_t>();

                auto& table = get_table(std::get<uint32_t>(instruction.arguments), mod);
                uint32_t oldSize = table.size();

                Reference value = m_frame->stack.pop_as<Reference>();

                // NOTE: If the limits max is not present, it's UINT32_MAX
                if ((uint64_t)table.size() + addEntries > mod->wasmFile.tables[std::get<uint32_t>(instruction.arguments)].limits.max)
                {
                    m_frame->stack.push((uint32_t)-1);
                    break;
                }

                if (table.size() + addEntries <= mod->wasmFile.tables[std::get<uint32_t>(instruction.arguments)].limits.max)
                {
                    if (addEntries >= 0)
                    {
                        for (uint32_t i = 0; i < addEntries; i++)
                            table.push_back(value);
                    }
                    else
                    {
                        assert(false);
                        for (uint32_t i = 0; i < addEntries; i++)
                            table.pop_back();
                    }
                }

                m_frame->stack.push(oldSize);
                break;
            }
            case Opcode::table_size:
                m_frame->stack.push((uint32_t)get_table(std::get<uint32_t>(instruction.arguments), mod).size());
                break;
            case Opcode::table_fill: {
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto& table = get_table(std::get<uint32_t>(instruction.arguments), mod);
                                
                if (destination + count > table.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                    table[destination + i] = value;

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

VM::Module* VM::get_registered_module(const std::string& name)
{
    return m_registered_modules[name];
}

Value VM::run_bare_code_returning(Module* mod, std::vector<Instruction> instructions, Type returnType)
{
    // clang-format off
    std::vector<Value> values = run_function(mod, {
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
    auto& memory = get_memory(m_frame->mod);

    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > memory.size * WASM_PAGE_SIZE)
        throw Trap();

    ActualType value;
    memcpy(&value, &memory.data[address + memArg.offset], sizeof(ActualType));

    m_frame->stack.push((StackType)value);
}

template <typename ActualType, typename StackType>
void VM::run_store_instruction(const MemArg& memArg)
{
    auto& memory = get_memory(m_frame->mod);

    ActualType value = (ActualType)m_frame->stack.pop_as<StackType>();
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > memory.size * WASM_PAGE_SIZE)
        throw Trap();

    memcpy(&memory.data[address + memArg.offset], &value, sizeof(ActualType));
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
    uint32_t functionTypeIndex = index < m_frame->mod->wasmFile.get_import_count_of_type(ImportType::Function) ? m_frame->mod->wasmFile.imports.at(index).functionIndex : m_frame->mod->wasmFile.functionTypeIndexes[index];
    FunctionType functionType = m_frame->mod->wasmFile.functionTypes[functionTypeIndex];

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
    if (index < m_frame->mod->wasmFile.imports.size())
    {
        const auto& import = m_frame->mod->wasmFile.imports.at(index);
        if (import.type != ImportType::Function)
        {
            fprintf(stderr, "Tried to call an import of different type than function\n");
            throw Trap();
        }

        if (import.environment == "wasi_snapshot_preview1")
        {
            returnedValues = WASI::run_wasi_call(import.name, args);
        }
        else
        {
            returnedValues = run_function(m_registered_modules[import.environment], import.name, args);
        }
    }
    else
    {
        returnedValues = run_function(m_frame->mod, index, args);
    }

    for (const auto& returned : returnedValues)
        m_frame->stack.push(returned);
}

Value& VM::get_global(uint32_t index, Module* module)
{
    if (index < module->wasmFile.get_import_count_of_type(ImportType::Global))
    {
        const auto& import = module->wasmFile.imports.at(index);
        
        for (const auto& [name, mod] : m_registered_modules)
        {
            if (name == import.environment)
            {
                Export exp = mod->wasmFile.find_export_by_name(import.name);
                assert(exp.type == 0x03); // FIXME: Use an enum
                return mod->globals[exp.index];
            }
        }
    }
    
    return module->globals[index];
}

std::vector<Reference>& VM::get_table(uint32_t index, Module* module)
{
    if (index < module->wasmFile.get_import_count_of_type(ImportType::Table))
    {
        const auto& import = module->wasmFile.imports.at(index);
        
        for (const auto& [name, mod] : m_registered_modules)
        {
            if (name == import.environment)
            {
                Export exp = mod->wasmFile.find_export_by_name(import.name);
                assert(exp.type == 0x01); // FIXME: Use an enum
                return mod->tables[exp.index];
            }
        }
    }
    
    return module->tables[index];
}

VM::Memory& VM::get_memory(Module* module)
{
    if (module->wasmFile.memories.size() > 0)
        return module->memory;

    if (module->wasmFile.get_import_count_of_type(ImportType::Memory) > 0)
    {
        for (const auto& import : module->wasmFile.imports)
        {
            if (import.type != ImportType::Memory)
                continue;

            for (const auto& [name, mod] : m_registered_modules)
            {
                if (name == import.environment)
                {
                    Export exp = mod->wasmFile.find_export_by_name(import.name);
                    assert(exp.type == 0x02); // FIXME: Use an enum
                    return mod->memory;
                }
            }
        }
        
    }

    assert(false);
}
