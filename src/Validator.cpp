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

void Validator::validate(Ref<WasmFile::WasmFile> wasmFile)
{
    for (size_t i = 0; i < wasmFile->functionTypeIndexes.size(); i++)
    {
        // if (i == 49)
        //     asm volatile("int3");
        validate_function(wasmFile, wasmFile->functionTypes[wasmFile->functionTypeIndexes[i]], wasmFile->codeBlocks[i]);
    }
}

void Validator::validate_function(Ref<WasmFile::WasmFile> wasmFile, const WasmFile::FunctionType& type, const WasmFile::Code& code)
{
    // FIXME: Calculate it once for the whole validation
    std::vector<uint32_t> functionTypeIndexes;
    std::vector<std::pair<Type, WasmFile::GlobalMutability>> globals;
    uint32_t memoryCount = 0;
    std::vector<Type> tables;

    for (const auto& import : wasmFile->imports)
    {
        switch (import.type)
        {
            case WasmFile::ImportType::Function:
                functionTypeIndexes.push_back(import.functionTypeIndex);
                break;
            case WasmFile::ImportType::Global:
                globals.push_back({ import.globalType, import.globalMut });
                break;
            case WasmFile::ImportType::Memory:
                memoryCount++;
                break;
            case WasmFile::ImportType::Table:
                tables.push_back(import.tableRefType);
                break;
            default:
                assert(false);
        }
    }

    for (const auto index : wasmFile->functionTypeIndexes)
        functionTypeIndexes.push_back(index);

    for (const auto& global : wasmFile->globals)
        globals.push_back({ global.type, global.mut });

    memoryCount += wasmFile->memories.size();

    for (const auto& table : wasmFile->tables)
        tables.push_back(table.refType);

    std::vector<ValidatorLabel> labels;
    auto expect_labels = [&labels](uint32_t count, bool consume) {
        if (count > labels.size())
            throw WasmFile::InvalidWASMException();

        if (consume)
        {
            for (uint32_t i = 0; i < count; i++)
                labels.pop_back();
        }
    };

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

        if (stack.size() == (labels.size() == 0 ? 0 : labels[labels.size() - 1].stackHeight))
            throw WasmFile::InvalidWASMException();

        if (stack.back() != expected)
            throw WasmFile::InvalidWASMException();
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

    auto validate_load_operation = [expect, &stack, memoryCount](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        if (memArg.memoryIndex >= memoryCount)
            throw WasmFile::InvalidWASMException();
        if ((1ull << memArg.align) > bitWidth / 8)
            throw WasmFile::InvalidWASMException();
        expect(Type::i32);
        stack.push_back(type);
    };

    auto validate_store_operation = [expect, &stack, memoryCount](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        if (memArg.memoryIndex >= memoryCount)
            throw WasmFile::InvalidWASMException();
        if ((1ull << memArg.align) > bitWidth / 8)
            throw WasmFile::InvalidWASMException();
        expect(type);
        expect(Type::i32);
    };

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
                const auto& params = arguments.blockType.get_param_types(wasmFile);

                // FIXME: This is kinda ugly
                std::vector<Type> types;
                for (auto type = params.rbegin(); type != params.rend(); type++)
                {
                    if (stack.size() == 0)
                        throw WasmFile::InvalidWASMException();
                    Type popped = stack.back();
                    expect(*type);
                    types.push_back(popped);
                }
                std::reverse(types.begin(), types.end());
                for (const auto type : types)
                    stack.push_back(type);

                labels.push_back(ValidatorLabel {
                    .stackHeight = static_cast<uint32_t>(stack.size() - params.size()),
                    .returnTypes = arguments.blockType.get_return_types(wasmFile),
                    .paramTypes = params,
                    .unreachable = false });
                break;
            }
            case Opcode::if_: {
                const IfArguments& arguments = std::get<IfArguments>(instruction.arguments);
                const auto& params = arguments.blockType.get_param_types(wasmFile);

                expect(Type::i32);

                // FIXME: This is duplicate with block
                std::vector<Type> types;
                for (auto type = params.rbegin(); type != params.rend(); type++)
                {
                    if (stack.size() == 0)
                        throw WasmFile::InvalidWASMException();
                    Type popped = stack.back();
                    expect(*type);
                    types.push_back(popped);
                }
                std::reverse(types.begin(), types.end());
                for (const auto type : types)
                    stack.push_back(type);

                labels.push_back(ValidatorLabel {
                    .stackHeight = static_cast<uint32_t>(stack.size() - params.size()),
                    .returnTypes = arguments.blockType.get_return_types(wasmFile),
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
                        if (stack.size() == 0)
                            throw WasmFile::InvalidWASMException();
                        Type popped = stack.back();
                        if (popped != *type)
                            throw WasmFile::InvalidWASMException();
                        stack.pop_back();
                    }

                    if (stack.size() != label.stackHeight)
                        throw WasmFile::InvalidWASMException();
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
                if (labels.size() == 0)
                    throw WasmFile::InvalidWASMException();

                auto& label = labels[labels.size() - 1];
                if (!label.unreachable)
                {
                    // FIXME: This is kinda ugly
                    std::vector<Type> types;
                    for (auto type = label.returnTypes.rbegin(); type != label.returnTypes.rend(); type++)
                    {
                        if (stack.size() == 0)
                            throw WasmFile::InvalidWASMException();
                        Type popped = stack.back();
                        if (popped != *type)
                            throw WasmFile::InvalidWASMException();
                        stack.pop_back();
                        types.push_back(popped);
                    }

                    if (stack.size() != label.stackHeight)
                        throw WasmFile::InvalidWASMException();

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
            case Opcode::br:
                expect_labels(std::get<uint32_t>(instruction.arguments) + 1, false);
                labels[labels.size() - 1].unreachable = true;
                break;
            case Opcode::br_if:
                expect(Type::i32);
                expect_labels(std::get<uint32_t>(instruction.arguments) + 1, false);
                break;
            case Opcode::br_table:
                expect(Type::i32);
                labels[labels.size() - 1].unreachable = true;
                // FIXME: Validate this
                printf("Warning: Validation for br_table not implemented\n");
                break;
            case Opcode::return_:
                for (auto t = type.returns.rbegin(); t != type.returns.rend(); t++)
                    expect(*t);
                labels[labels.size() - 1].unreachable = true;
                break;
            case Opcode::call: {
                if (std::get<uint32_t>(instruction.arguments) >= functionTypeIndexes.size())
                    throw WasmFile::InvalidWASMException();
                const auto& calleeType = wasmFile->functionTypes[functionTypeIndexes[std::get<uint32_t>(instruction.arguments)]];
                for (auto type = calleeType.params.rbegin(); type != calleeType.params.rend(); type++)
                    expect(*type);

                for (const auto returned : calleeType.returns)
                    stack.push_back(returned);
                break;
            }
            case Opcode::call_indirect: {
                const CallIndirectArguments& arguments = std::get<CallIndirectArguments>(instruction.arguments);

                expect(Type::i32);

                if (arguments.tableIndex >= tables.size())
                    throw WasmFile::InvalidWASMException();

                if (arguments.typeIndex >= wasmFile->functionTypes.size())
                    throw WasmFile::InvalidWASMException();

                const auto& calleeType = wasmFile->functionTypes[arguments.typeIndex];
                for (auto type = calleeType.params.rbegin(); type != calleeType.params.rend(); type++)
                    expect(*type);

                for (const auto returned : calleeType.returns)
                    stack.push_back(returned);
                break;
            }
            case Opcode::drop:
                if (stack.size() == 0)
                    throw WasmFile::InvalidWASMException();
                stack.pop_back();
                break;
            case Opcode::select_typed:
                // FIXME: Validate types
                printf("Warning: Validation for select_typed not implemented\n");
                [[fallthrough]];
            case Opcode::select_: {
                // FIXME: This is a mess
                if (!labels[labels.size() - 1].unreachable)
                {
                    expect(Type::i32);
                    if (stack.size() < 2)
                        throw WasmFile::InvalidWASMException();
                    Type a = stack.back();
                    stack.pop_back();
                    Type b = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    if (a != b)
                        throw WasmFile::InvalidWASMException();
                }
                break;
            }
            case Opcode::local_get:
                if (std::get<uint32_t>(instruction.arguments) >= locals.size())
                    throw WasmFile::InvalidWASMException();
                stack.push_back(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::local_set:
                if (std::get<uint32_t>(instruction.arguments) >= locals.size())
                    throw WasmFile::InvalidWASMException();
                expect(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::local_tee:
                if (std::get<uint32_t>(instruction.arguments) >= locals.size())
                    throw WasmFile::InvalidWASMException();
                expect(locals[std::get<uint32_t>(instruction.arguments)]);
                stack.push_back(locals[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::global_get: {
                if (std::get<uint32_t>(instruction.arguments) >= globals.size())
                    throw WasmFile::InvalidWASMException();
                const auto& global = globals[std::get<uint32_t>(instruction.arguments)];
                stack.push_back(global.first);
                break;
            }
            case Opcode::global_set: {
                if (std::get<uint32_t>(instruction.arguments) >= globals.size())
                    throw WasmFile::InvalidWASMException();
                const auto& global = globals[std::get<uint32_t>(instruction.arguments)];
                if (global.second != WasmFile::GlobalMutability::Variable)
                    throw WasmFile::InvalidWASMException();
                expect(global.first);
                break;
            }
            case Opcode::table_get: {
                if (std::get<uint32_t>(instruction.arguments) >= tables.size())
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                stack.push_back(tables[std::get<uint32_t>(instruction.arguments)]);
                break;
            }
            case Opcode::table_set: {
                if (std::get<uint32_t>(instruction.arguments) >= tables.size())
                    throw WasmFile::InvalidWASMException();
                expect(tables[std::get<uint32_t>(instruction.arguments)]);
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
                if (std::get<uint32_t>(instruction.arguments) >= memoryCount)
                    throw WasmFile::InvalidWASMException();
                stack.push_back(Type::i32);
                break;
            case Opcode::memory_grow:
                if (std::get<uint32_t>(instruction.arguments) >= memoryCount)
                    throw WasmFile::InvalidWASMException();
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
                    if (stack.size() == 0)
                        throw WasmFile::InvalidWASMException();
                    Type a = stack.back();
                    stack.pop_back();
                    if (!is_reference_type(a))
                        throw WasmFile::InvalidWASMException();
                }
                stack.push_back(Type::i32);
                break;
            }
            case Opcode::ref_func:
                // FIXME: Verify that the ref is valid
                printf("Warning: Validation for ref_func not implemented\n");
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
                if (!wasmFile->dataCount)
                    throw WasmFile::InvalidWASMException();
                const auto& arguments = std::get<MemoryInitArguments>(instruction.arguments);
                if (arguments.memoryIndex >= memoryCount)
                    throw WasmFile::InvalidWASMException();
                if (arguments.dataIndex >= wasmFile->dataBlocks.size())
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::data_drop:
                if (!wasmFile->dataCount)
                    throw WasmFile::InvalidWASMException();
                if (std::get<uint32_t>(instruction.arguments) >= wasmFile->dataBlocks.size())
                    throw WasmFile::InvalidWASMException();
                break;
            case Opcode::memory_copy: {
                const auto& arguments = std::get<MemoryCopyArguments>(instruction.arguments);
                if (arguments.destination >= memoryCount)
                    throw WasmFile::InvalidWASMException();
                if (arguments.source >= memoryCount)
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::memory_fill:
                if (std::get<uint32_t>(instruction.arguments) >= memoryCount)
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            case Opcode::table_init: {
                const auto& arguments = std::get<TableInitArguments>(instruction.arguments);
                if (arguments.tableIndex >= tables.size())
                    throw WasmFile::InvalidWASMException();
                if (arguments.elementIndex >= wasmFile->elements.size())
                    throw WasmFile::InvalidWASMException();
                if (tables[arguments.tableIndex] != wasmFile->elements[arguments.elementIndex].valueType)
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::elem_drop:
                if (std::get<uint32_t>(instruction.arguments) >= wasmFile->elements.size())
                    throw WasmFile::InvalidWASMException();
                break;
            case Opcode::table_copy: {
                const auto& arguments = std::get<TableCopyArguments>(instruction.arguments);
                if (arguments.destination >= tables.size())
                    throw WasmFile::InvalidWASMException();
                if (arguments.source >= tables.size())
                    throw WasmFile::InvalidWASMException();
                if (tables[arguments.destination] != tables[arguments.source])
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(Type::i32);
                expect(Type::i32);
                break;
            }
            case Opcode::table_grow:
                if (std::get<uint32_t>(instruction.arguments) >= tables.size())
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(tables[std::get<uint32_t>(instruction.arguments)]);
                stack.push_back(Type::i32);
                break;
            case Opcode::table_size:
                if (std::get<uint32_t>(instruction.arguments) >= tables.size())
                    throw WasmFile::InvalidWASMException();
                stack.push_back(Type::i32);
                break;
            case Opcode::table_fill:
                if (std::get<uint32_t>(instruction.arguments) >= tables.size())
                    throw WasmFile::InvalidWASMException();
                expect(Type::i32);
                expect(tables[std::get<uint32_t>(instruction.arguments)]);
                expect(Type::i32);
                break;
            default:
                fprintf(stderr, "Error: No validation for opcode 0x%x\n", static_cast<uint32_t>(instruction.opcode));
                throw WasmFile::InvalidWASMException();
        }
    }
}
