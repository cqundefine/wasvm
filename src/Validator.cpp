#include <Parser.h>
#include <Validator.h>
#include <cassert>

struct ValidatorLabel
{
    uint32_t stackHeight;
    std::vector<Type> returnTypes;
    std::vector<Type> paramTypes;
    bool unreachable;
};

#define VALIDATION_ASSERT(x) if (!(x)) throw WasmFile::InvalidWASMException();

Validator::Validator(Ref<WasmFile::WasmFile> wasmFile)
    : m_wasmFile(wasmFile)
{
    for (const auto& import : wasmFile->imports)
    {
        switch (import.type)
        {
            case WasmFile::ImportType::Function:
                VALIDATION_ASSERT(import.functionTypeIndex < wasmFile->functionTypes.size());
                m_functions.push_back(import.functionTypeIndex);
                break;
            case WasmFile::ImportType::Global:
                m_imported_global_count++;
                m_globals.push_back({ import.globalType, import.globalMut });
                break;
            case WasmFile::ImportType::Memory:
                m_memories++;
                break;
            case WasmFile::ImportType::Table:
                m_tables.push_back(import.tableRefType);
                break;
            default:
                VALIDATION_ASSERT(false);
        }
    }

    for (const auto index : wasmFile->functionTypeIndexes)
    {
        VALIDATION_ASSERT(index < wasmFile->functionTypes.size());
        m_functions.push_back(index);
    }

    for (const auto& global : wasmFile->globals)
    {
        validate_constant_expression(global.initCode, global.type, true);
        m_globals.push_back({ global.type, global.mut });
    }

    for (const auto& memory : wasmFile->memories)
    {
        VALIDATION_ASSERT(memory.limits.min <= WasmFile::MAX_WASM_PAGES);
        if (memory.limits.max)
            VALIDATION_ASSERT(memory.limits.max <= WasmFile::MAX_WASM_PAGES);
        m_memories++;
    }

    for (const auto& table : wasmFile->tables)
        m_tables.push_back(table.refType);

    std::vector<std::string> usedVectorNames;

    for (const auto& exp : wasmFile->exports)
    {
        VALIDATION_ASSERT(!vector_contains(usedVectorNames, exp.name));
        usedVectorNames.push_back(exp.name);

        switch(exp.type)
        {
            case WasmFile::ImportType::Function:
                VALIDATION_ASSERT(exp.index < m_functions.size());
                break;
            case WasmFile::ImportType::Global:
                VALIDATION_ASSERT(exp.index < m_globals.size());
                break;
            case WasmFile::ImportType::Memory:
                VALIDATION_ASSERT(exp.index < m_memories);
                break;
            case WasmFile::ImportType::Table:
                VALIDATION_ASSERT(exp.index < m_tables.size());
                break;
            default:
                VALIDATION_ASSERT(false); 
        }
    }

    // FIXME: Allow multi-memory
    VALIDATION_ASSERT(m_memories <= 1);

    for (const auto& element : wasmFile->elements)
    {
        if (element.mode == WasmFile::ElementMode::Active)
            VALIDATION_ASSERT(element.table < m_tables.size());

        for (const auto& expression : element.referencesExpr)
            validate_constant_expression(expression, element.valueType, true);

        if (element.expr.size() > 0)
            validate_constant_expression(element.expr, Type::i32, true);

        for (auto index : element.functionIndexes)
            VALIDATION_ASSERT(index < m_functions.size());
    }

    for (const auto& data : wasmFile->dataBlocks)
    {
        if (data.mode == WasmFile::ElementMode::Active)
        {
            VALIDATION_ASSERT(data.memoryIndex < m_memories);
            validate_constant_expression(data.expr, Type::i32, true);
        }
    }

    for (size_t i = 0; i < wasmFile->functionTypeIndexes.size(); i++)
    {
        // if (i == 44)
        //     asm volatile("int3");
        validate_function(wasmFile->functionTypes[wasmFile->functionTypeIndexes[i]], wasmFile->codeBlocks[i]);
    }

    if (wasmFile->startFunction)
    {
        VALIDATION_ASSERT(*wasmFile->startFunction < m_functions.size());

        const auto& type = wasmFile->functionTypes[m_functions[*wasmFile->startFunction]];
        VALIDATION_ASSERT(type.params.size() == 0);
        VALIDATION_ASSERT(type.returns.size() == 0);
    }
}

void Validator::validate_function(const WasmFile::FunctionType& type, const WasmFile::Code& code)
{
    std::vector<ValidatorLabel> labels;
    labels.push_back(ValidatorLabel {
        .stackHeight = 0,
        .returnTypes = type.returns,
        .paramTypes = type.params,
        .unreachable = false });

    std::vector<Type> stack;

    auto expect = [&stack, &labels](Type expected) {
        assert(labels.size() > 0);
        if (labels[labels.size() - 1].unreachable)
            return;

        VALIDATION_ASSERT(stack.size() > labels[labels.size() - 1].stackHeight);
        VALIDATION_ASSERT(stack.back() == expected);
        stack.pop_back();
    };

    std::vector<Type> locals;

    for (const auto param : type.params)
        locals.push_back(param);

    for (const auto local : code.locals)
        locals.push_back(local);

    auto validate_unary_operation = [expect, &stack](Type type) {
        expect(type);
        stack.push_back(type);
    };

    auto validate_binary_operation = [expect, &stack](Type type) {
        expect(type);
        expect(type);
        stack.push_back(type);
    };

    auto validate_test_operation = [expect, &stack](Type type) {
        expect(type);
        stack.push_back(Type::i32);
    };

    auto validate_comparison_operation = [expect, &stack](Type type) {
        expect(type);
        expect(type);
        stack.push_back(Type::i32);
    };

    auto validate_load_operation = [expect, &stack, this](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        VALIDATION_ASSERT(memArg.memoryIndex < m_memories);
        VALIDATION_ASSERT((1ull << memArg.align) <= bitWidth / 8);
        expect(Type::i32);
        stack.push_back(type);
    };

    auto validate_store_operation = [expect, &stack, this](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        VALIDATION_ASSERT(memArg.memoryIndex < m_memories);
        VALIDATION_ASSERT((1ull << memArg.align) <= bitWidth / 8);
        expect(type);
        expect(Type::i32);
    };

    // FIXME: All the comparisions of stack size being > 0 are incorrect
    for (const auto& instruction : code.instructions)
    {
        switch (instruction.opcode)
        {
            case Opcode::unreachable:
                labels[labels.size() - 1].unreachable = true;
                break;
            case Opcode::nop:
                break;
            case Opcode::block:
            case Opcode::loop: {
                const BlockLoopArguments& arguments = std::get<BlockLoopArguments>(instruction.arguments);
                const auto& params = arguments.blockType.get_param_types(m_wasmFile);

                // FIXME: This is kinda ugly
                std::vector<Type> types;
                for (auto type = params.rbegin(); type != params.rend(); type++)
                {
                    VALIDATION_ASSERT(stack.size() > 0);
                    Type popped = stack.back();
                    expect(*type);
                    types.push_back(popped);
                }
                std::reverse(types.begin(), types.end());
                for (const auto type : types)
                    stack.push_back(type);

                labels.push_back(ValidatorLabel {
                    .stackHeight = static_cast<uint32_t>(stack.size() - params.size()),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .unreachable = false });
                break;
            }
            case Opcode::if_: {
                const IfArguments& arguments = std::get<IfArguments>(instruction.arguments);
                const auto& params = arguments.blockType.get_param_types(m_wasmFile);

                expect(Type::i32);

                // FIXME: This is duplicate with block
                std::vector<Type> types;
                for (auto type = params.rbegin(); type != params.rend(); type++)
                {
                    VALIDATION_ASSERT(stack.size() > 0);
                    Type popped = stack.back();
                    expect(*type);
                    types.push_back(popped);
                }
                std::reverse(types.begin(), types.end());
                for (const auto type : types)
                    stack.push_back(type);

                labels.push_back(ValidatorLabel {
                    .stackHeight = static_cast<uint32_t>(stack.size() - params.size()),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .unreachable = false });
                break;
            }
            case Opcode::else_: {
                auto& label = labels[labels.size() - 1];
                if (!label.unreachable)
                {
                    // FIXME: This is kinda ugly
                    for (auto type = label.returnTypes.rbegin(); type != label.returnTypes.rend(); type++)
                    {
                        VALIDATION_ASSERT(stack.size() > 0)
                        Type popped = stack.back();
                        VALIDATION_ASSERT(popped == *type);
                        stack.pop_back();
                    }

                    VALIDATION_ASSERT(stack.size() == label.stackHeight);
                }
                else
                {
                    stack.erase(stack.begin() + label.stackHeight, stack.end());
                }
                label.unreachable = false;
                for (const auto type : label.paramTypes)
                    stack.push_back(type);
                break;
            }
            case Opcode::end: {
                VALIDATION_ASSERT(labels.size() > 0)

                auto& label = labels[labels.size() - 1];
                if (!label.unreachable)
                {
                    // FIXME: This is kinda ugly and a duplicate
                    std::vector<Type> types;
                    for (auto type = label.returnTypes.rbegin(); type != label.returnTypes.rend(); type++)
                    {
                        VALIDATION_ASSERT(stack.size() > 0);
                        Type popped = stack.back();
                        VALIDATION_ASSERT(popped == *type);
                        stack.pop_back();
                        types.push_back(popped);
                    }

                    VALIDATION_ASSERT(stack.size() == label.stackHeight);

                    std::reverse(types.begin(), types.end());
                    for (const auto type : types)
                        stack.push_back(type);
                }
                else
                {
                    stack.erase(stack.begin() + label.stackHeight, stack.end());
                    for (const auto type : label.returnTypes)
                        stack.push_back(type);
                }
                labels.pop_back();
                break;
            }
            case Opcode::br: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) + 1 <= labels.size());
                const auto& label = labels[labels.size() - std::get<uint32_t>(instruction.arguments) - 1];
                for (auto type = label.returnTypes.rbegin(); type != label.returnTypes.rend(); type++)
                    expect(*type);
                labels[labels.size() - 1].unreachable = true;
                break;
            }
            case Opcode::br_if: {
                expect(Type::i32);
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) + 1 <= labels.size());
                const auto& label = labels[labels.size() - std::get<uint32_t>(instruction.arguments) - 1];
                VALIDATION_ASSERT(stack.size() >= label.returnTypes.size())
                for (size_t i = 0; i < label.returnTypes.size(); i++)
                    VALIDATION_ASSERT(stack[stack.size() - i - 1] == label.returnTypes[label.returnTypes.size() - i - 1]);
                break;
            }
            case Opcode::br_table: {
                const auto& arguments = std::get<BranchTableArguments>(instruction.arguments);
                expect(Type::i32);
                labels[labels.size() - 1].unreachable = true;
                VALIDATION_ASSERT(arguments.defaultLabel <= labels.size());
                for (const auto& label : arguments.labels)
                    VALIDATION_ASSERT(label <= labels.size());
                break;
            }
            case Opcode::return_:
                for (auto t = type.returns.rbegin(); t != type.returns.rend(); t++)
                    expect(*t);
                labels[labels.size() - 1].unreachable = true;
                break;
            case Opcode::call: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_functions.size());
                const auto& calleeType = m_wasmFile->functionTypes[m_functions[std::get<uint32_t>(instruction.arguments)]];
                for (auto type = calleeType.params.rbegin(); type != calleeType.params.rend(); type++)
                    expect(*type);

                for (const auto returned : calleeType.returns)
                    stack.push_back(returned);
                break;
            }
            case Opcode::call_indirect: {
                const CallIndirectArguments& arguments = std::get<CallIndirectArguments>(instruction.arguments);

                expect(Type::i32);

                VALIDATION_ASSERT(arguments.tableIndex < m_tables.size());
                VALIDATION_ASSERT(arguments.typeIndex < m_wasmFile->functionTypes.size());
                VALIDATION_ASSERT(m_tables[arguments.tableIndex] == Type::funcref);

                const auto& calleeType = m_wasmFile->functionTypes[arguments.typeIndex];
                for (auto type = calleeType.params.rbegin(); type != calleeType.params.rend(); type++)
                    expect(*type);

                for (const auto returned : calleeType.returns)
                    stack.push_back(returned);
                break;
            }
            case Opcode::drop:
                VALIDATION_ASSERT(stack.size() > 0)
                stack.pop_back();
                break;
            case Opcode::select_: {
                // FIXME: This is a mess
                if (!labels[labels.size() - 1].unreachable)
                {
                    expect(Type::i32);
                    VALIDATION_ASSERT(stack.size() >= 2);
                    Type a = stack.back();
                    stack.pop_back();
                    Type b = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    VALIDATION_ASSERT(a == b);
                    VALIDATION_ASSERT(a == Type::i32 || a == Type::i64 || a == Type::f32 || a == Type::f64 || a == Type::v128);
                }
                break;
            }
            case Opcode::select_typed: {
                // FIXME: This is duplicated with select and also a mess
                if (!labels[labels.size() - 1].unreachable)
                {
                    const auto& arguments = std::get<std::vector<uint8_t>>(instruction.arguments);
                    VALIDATION_ASSERT(arguments.size() == 1);
                    VALIDATION_ASSERT(is_valid_type((Type)arguments[0]));
                    expect(Type::i32);
                    VALIDATION_ASSERT(stack.size() >= 2);
                    Type a = stack.back();
                    stack.pop_back();
                    Type b = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    VALIDATION_ASSERT(a == (Type)arguments[0]);
                    VALIDATION_ASSERT(b == (Type)arguments[0]);
                }
                break;
            }
            case Opcode::local_get:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < locals.size());
                stack.push_back(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::local_set:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < locals.size());
                expect(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::local_tee:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < locals.size());
                expect(locals[std::get<uint32_t>(instruction.arguments)]);
                stack.push_back(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::global_get: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_globals.size());
                const auto& global = m_globals[std::get<uint32_t>(instruction.arguments)];
                stack.push_back(global.first);
                break;
            }
            case Opcode::global_set: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_globals.size());
                const auto& global = m_globals[std::get<uint32_t>(instruction.arguments)];
                VALIDATION_ASSERT(global.second == WasmFile::GlobalMutability::Variable);
                expect(global.first);
                break;
            }
            case Opcode::table_get: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_tables.size());
                expect(Type::i32);
                stack.push_back(m_tables[std::get<uint32_t>(instruction.arguments)]);
                break;
            }
            case Opcode::table_set: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_tables.size());
                expect(m_tables[std::get<uint32_t>(instruction.arguments)]);
                expect(Type::i32);
                break;
            }
            case Opcode::i32_load:
                validate_load_operation(Type::i32, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load:
                validate_load_operation(Type::i64, 64, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f32_load:
                validate_load_operation(Type::f32, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f64_load:
                validate_load_operation(Type::f64, 64, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load8_s:
            case Opcode::i32_load8_u:
                validate_load_operation(Type::i32, 8, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load16_s:
            case Opcode::i32_load16_u:
                validate_load_operation(Type::i32, 16, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load8_s:
            case Opcode::i64_load8_u:
                validate_load_operation(Type::i64, 8, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load16_s:
            case Opcode::i64_load16_u:
                validate_load_operation(Type::i64, 16, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load32_s:
            case Opcode::i64_load32_u:
                validate_load_operation(Type::i64, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store:
                validate_store_operation(Type::i32, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store:
                validate_store_operation(Type::i64, 64, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f32_store:
                validate_store_operation(Type::f32, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f64_store:
                validate_store_operation(Type::f64, 64, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store8:
                validate_store_operation(Type::i32, 8, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store16:
                validate_store_operation(Type::i32, 16, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store8:
                validate_store_operation(Type::i64, 8, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store16:
                validate_store_operation(Type::i64, 16, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store32:
                validate_store_operation(Type::i64, 32, std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::memory_size:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_memories);
                stack.push_back(Type::i32);
                break;
            case Opcode::memory_grow:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_memories);
                expect(Type::i32);
                stack.push_back(Type::i32);
                break;
            case Opcode::i32_const:
                stack.push_back(Type::i32);
                break;
            case Opcode::i64_const:
                stack.push_back(Type::i64);
                break;
            case Opcode::f32_const:
                stack.push_back(Type::f32);
                break;
            case Opcode::f64_const:
                stack.push_back(Type::f64);
                break;
            case Opcode::i32_eqz:
                validate_test_operation(Type::i32);
                break;
            case Opcode::i32_eq:
            case Opcode::i32_ne:
            case Opcode::i32_lt_s:
            case Opcode::i32_lt_u:
            case Opcode::i32_gt_s:
            case Opcode::i32_gt_u:
            case Opcode::i32_le_s:
            case Opcode::i32_le_u:
            case Opcode::i32_ge_s:
            case Opcode::i32_ge_u:
                validate_comparison_operation(Type::i32);
                break;
            case Opcode::i64_eqz:
                validate_test_operation(Type::i64);
                break;
            case Opcode::i64_eq:
            case Opcode::i64_ne:
            case Opcode::i64_lt_s:
            case Opcode::i64_lt_u:
            case Opcode::i64_gt_s:
            case Opcode::i64_gt_u:
            case Opcode::i64_le_s:
            case Opcode::i64_le_u:
            case Opcode::i64_ge_s:
            case Opcode::i64_ge_u:
                validate_comparison_operation(Type::i64);
                break;
            case Opcode::f32_eq:
            case Opcode::f32_ne:
            case Opcode::f32_lt:
            case Opcode::f32_gt:
            case Opcode::f32_le:
            case Opcode::f32_ge:
                validate_comparison_operation(Type::f32);
                break;
            case Opcode::f64_eq:
            case Opcode::f64_ne:
            case Opcode::f64_lt:
            case Opcode::f64_gt:
            case Opcode::f64_le:
            case Opcode::f64_ge:
                validate_comparison_operation(Type::f64);
                break;
            case Opcode::i32_clz:
            case Opcode::i32_ctz:
            case Opcode::i32_popcnt:
            case Opcode::i32_extend8_s:
            case Opcode::i32_extend16_s:
                validate_unary_operation(Type::i32);
                break;
            case Opcode::i32_add:
            case Opcode::i32_sub:
            case Opcode::i32_mul:
            case Opcode::i32_div_s:
            case Opcode::i32_div_u:
            case Opcode::i32_rem_s:
            case Opcode::i32_rem_u:
            case Opcode::i32_and:
            case Opcode::i32_or:
            case Opcode::i32_xor:
            case Opcode::i32_shl:
            case Opcode::i32_shr_s:
            case Opcode::i32_shr_u:
            case Opcode::i32_rotl:
            case Opcode::i32_rotr:
                validate_binary_operation(Type::i32);
                break;
            case Opcode::i64_clz:
            case Opcode::i64_ctz:
            case Opcode::i64_popcnt:
            case Opcode::i64_extend8_s:
            case Opcode::i64_extend16_s:
            case Opcode::i64_extend32_s:
                validate_unary_operation(Type::i64);
                break;
            case Opcode::i64_add:
            case Opcode::i64_sub:
            case Opcode::i64_mul:
            case Opcode::i64_div_s:
            case Opcode::i64_div_u:
            case Opcode::i64_rem_s:
            case Opcode::i64_rem_u:
            case Opcode::i64_and:
            case Opcode::i64_or:
            case Opcode::i64_xor:
            case Opcode::i64_shl:
            case Opcode::i64_shr_s:
            case Opcode::i64_shr_u:
            case Opcode::i64_rotl:
            case Opcode::i64_rotr:
                validate_binary_operation(Type::i64);
                break;
            case Opcode::f32_abs:
            case Opcode::f32_neg:
            case Opcode::f32_ceil:
            case Opcode::f32_floor:
            case Opcode::f32_trunc:
            case Opcode::f32_nearest:
            case Opcode::f32_sqrt:
                validate_unary_operation(Type::f32);
                break;
            case Opcode::f32_add:
            case Opcode::f32_sub:
            case Opcode::f32_mul:
            case Opcode::f32_div:
            case Opcode::f32_min:
            case Opcode::f32_max:
            case Opcode::f32_copysign:
                validate_binary_operation(Type::f32);
                break;
            case Opcode::f64_abs:
            case Opcode::f64_neg:
            case Opcode::f64_ceil:
            case Opcode::f64_floor:
            case Opcode::f64_trunc:
            case Opcode::f64_nearest:
            case Opcode::f64_sqrt:
                validate_unary_operation(Type::f64);
                break;
            case Opcode::f64_add:
            case Opcode::f64_sub:
            case Opcode::f64_mul:
            case Opcode::f64_div:
            case Opcode::f64_min:
            case Opcode::f64_max:
            case Opcode::f64_copysign:
                validate_binary_operation(Type::f64);
                break;
            case Opcode::i32_wrap_i64:
                expect(Type::i64);
                stack.push_back(Type::i32);
                break;
            case Opcode::i32_trunc_f32_s:
            case Opcode::i32_trunc_f32_u:
                expect(Type::f32);
                stack.push_back(Type::i32);
                break;
            case Opcode::i32_trunc_f64_s:
            case Opcode::i32_trunc_f64_u:
                expect(Type::f64);
                stack.push_back(Type::i32);
                break;
            case Opcode::i64_extend_i32_s:
            case Opcode::i64_extend_i32_u:
                expect(Type::i32);
                stack.push_back(Type::i64);
                break;
            case Opcode::i64_trunc_f32_s:
            case Opcode::i64_trunc_f32_u:
                expect(Type::f32);
                stack.push_back(Type::i64);
                break;
            case Opcode::i64_trunc_f64_s:
            case Opcode::i64_trunc_f64_u:
                expect(Type::f64);
                stack.push_back(Type::i64);
                break;
            case Opcode::f32_convert_i32_s:
            case Opcode::f32_convert_i32_u:
                expect(Type::i32);
                stack.push_back(Type::f32);
                break;
            case Opcode::f32_convert_i64_s:
            case Opcode::f32_convert_i64_u:
                expect(Type::i64);
                stack.push_back(Type::f32);
                break;
            case Opcode::f32_demote_f64:
                expect(Type::f64);
                stack.push_back(Type::f32);
                break;
            case Opcode::f64_convert_i32_s:
            case Opcode::f64_convert_i32_u:
                expect(Type::i32);
                stack.push_back(Type::f64);
                break;
            case Opcode::f64_convert_i64_s:
            case Opcode::f64_convert_i64_u:
                expect(Type::i64);
                stack.push_back(Type::f64);
                break;
            case Opcode::f64_promote_f32:
                expect(Type::f32);
                stack.push_back(Type::f64);
                break;
            case Opcode::i32_reinterpret_f32:
                expect(Type::f32);
                stack.push_back(Type::i32);
                break;
            case Opcode::i64_reinterpret_f64:
                expect(Type::f64);
                stack.push_back(Type::i64);
                break;
            case Opcode::f32_reinterpret_i32:
                expect(Type::i32);
                stack.push_back(Type::f32);
                break;
            case Opcode::f64_reinterpret_i64:
                expect(Type::i64);
                stack.push_back(Type::f64);
                break;
            case Opcode::ref_null:
                stack.push_back(std::get<Type>(instruction.arguments));
                break;
            case Opcode::ref_is_null: {
                if (!labels[labels.size() - 1].unreachable)
                {
                    VALIDATION_ASSERT(stack.size() > 0);
                    Type a = stack.back();
                    stack.pop_back();
                    VALIDATION_ASSERT(is_reference_type(a));
                }
                stack.push_back(Type::i32);
                break;
            }
            case Opcode::ref_func:
                // TODO: Check if the function was declared
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_functions.size());
                stack.push_back(Type::funcref);
                break;
            case Opcode::i32_trunc_sat_f32_s:
            case Opcode::i32_trunc_sat_f32_u:
                expect(Type::f32);
                stack.push_back(Type::i32);
                break;
            case Opcode::i32_trunc_sat_f64_s:
            case Opcode::i32_trunc_sat_f64_u:
                expect(Type::f64);
                stack.push_back(Type::i32);
                break;
            case Opcode::i64_trunc_sat_f32_s:
            case Opcode::i64_trunc_sat_f32_u:
                expect(Type::f32);
                stack.push_back(Type::i64);
                break;
            case Opcode::i64_trunc_sat_f64_s:
            case Opcode::i64_trunc_sat_f64_u:
                expect(Type::f64);
                stack.push_back(Type::i64);
                break;
            case Opcode::memory_init: {
                VALIDATION_ASSERT(m_wasmFile->dataCount);
                const auto& arguments = std::get<MemoryInitArguments>(instruction.arguments);
                VALIDATION_ASSERT(arguments.memoryIndex < m_memories);
                VALIDATION_ASSERT(arguments.dataIndex < m_wasmFile->dataBlocks.size());
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::data_drop:
                VALIDATION_ASSERT(m_wasmFile->dataCount);
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_wasmFile->dataBlocks.size());
                break;
            case Opcode::memory_copy: {
                const auto& arguments = std::get<MemoryCopyArguments>(instruction.arguments);
                VALIDATION_ASSERT(arguments.destination < m_memories);
                VALIDATION_ASSERT(arguments.source < m_memories);
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::memory_fill:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_memories);
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            case Opcode::table_init: {
                const auto& arguments = std::get<TableInitArguments>(instruction.arguments);
                VALIDATION_ASSERT(arguments.tableIndex < m_tables.size());
                VALIDATION_ASSERT(arguments.elementIndex < m_wasmFile->elements.size());
                VALIDATION_ASSERT(m_tables[arguments.tableIndex] == m_wasmFile->elements[arguments.elementIndex].valueType);
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::elem_drop:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_wasmFile->elements.size());
                break;
            case Opcode::table_copy: {
                const auto& arguments = std::get<TableCopyArguments>(instruction.arguments);
                VALIDATION_ASSERT(arguments.destination < m_tables.size());
                VALIDATION_ASSERT(arguments.source < m_tables.size());
                VALIDATION_ASSERT(m_tables[arguments.destination] == m_tables[arguments.source]);
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::table_grow:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_tables.size());
                expect(Type::i32);
                expect(m_tables[std::get<uint32_t>(instruction.arguments)]);
                stack.push_back(Type::i32);
                break;
            case Opcode::table_size:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_tables.size());
                stack.push_back(Type::i32);
                break;
            case Opcode::table_fill:
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_tables.size());
                expect(Type::i32);
                expect(m_tables[std::get<uint32_t>(instruction.arguments)]);
                expect(Type::i32);
                break;
            default:
                std::println(std::cerr, "Error: No validation for opcode {:#x}", static_cast<uint32_t>(instruction.opcode));
                VALIDATION_ASSERT(false);
        }
    }
}

void Validator::validate_constant_expression(const std::vector<Instruction>& instructions, Type expectedReturnType, bool globalRestrictions)
{
    std::vector<Type> stack;

    for (size_t ip = 0; ip < instructions.size(); ip++)
    {
        const auto& instruction = instructions[ip];
        switch (instruction.opcode)
        {
            case Opcode::end:
                VALIDATION_ASSERT(ip == instructions.size() - 1);
                break;
            case Opcode::global_get: {
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_globals.size());
                if (globalRestrictions)
                    VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_imported_global_count);
                const auto& global = m_globals[std::get<uint32_t>(instruction.arguments)];
                VALIDATION_ASSERT(global.second == WasmFile::GlobalMutability::Constant);
                stack.push_back(global.first);
                break;
            }
            case Opcode::i32_const:
                stack.push_back(Type::i32);
                break;
            case Opcode::i64_const:
                stack.push_back(Type::i64);
                break;
            case Opcode::f32_const:
                stack.push_back(Type::f32);
                break;
            case Opcode::f64_const:
                stack.push_back(Type::f64);
                break;
            case Opcode::ref_null:
                stack.push_back(std::get<Type>(instruction.arguments));
                break;
            case Opcode::ref_func:
                // TODO: Check if the function was declared
                VALIDATION_ASSERT(std::get<uint32_t>(instruction.arguments) < m_functions.size());
                stack.push_back(Type::funcref);
                break;
            case Opcode::v128_const:
                stack.push_back(Type::v128);
                break;
            default:
                VALIDATION_ASSERT(false);
        }
    }

    VALIDATION_ASSERT(stack.size() == 1);
    VALIDATION_ASSERT(stack.back() == expectedReturnType);
}
