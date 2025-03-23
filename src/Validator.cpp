#include "Type.h"
#include <Parser.h>
#include <Validator.h>
#include <WasmFile.h>
#include <cassert>
#include <iostream>
#include <ostream>
#include <print>
#include <ranges>

#define VALIDATION_ASSERT(x) \
    if (!(x))                \
        throw WasmFile::InvalidWASMException();

class ValidatorType
{
public:
    ValidatorType(Type type)
        : m_type(type)
        , m_known(true)
    {
    }

    ValidatorType()
        : m_type(Type::i32)
        , m_known(false)
    {
    }

    bool operator==(Type const& other) const
    {
        if (m_known)
            return m_type == other;
        return true;
    }

    bool operator==(ValidatorType const& other) const
    {
        if (m_known && other.m_known)
            return m_type == other;
        return true;
    }

    bool is_reference_type() const
    {
        if (m_known)
            return ::is_reference_type(m_type);
        return true;
    }

    Type type() const { return m_type; }
    bool known() const { return m_known; }

private:
    Type m_type;
    bool m_known;
};

enum class ValidatorLabelType
{
    Entry,
    Block,
    Loop,
    If,
    IfAfterElse,
};

struct ValidatorLabel
{
    uint32_t stackHeight;
    std::vector<Type> returnTypes;
    std::vector<Type> paramTypes;
    ValidatorLabelType type;
    bool unreachable;
};

class ValidatorValueStack
{
public:
    void push(ValidatorType type)
    {
        m_stack.push_back(type);
    }

    ValidatorType pop()
    {
        if (m_stack.size() == last_label().stackHeight && last_label().unreachable)
            return ValidatorType();

        VALIDATION_ASSERT(m_stack.size() > last_label().stackHeight);
        auto type = m_stack.back();
        m_stack.pop_back();
        return type;
    }

    ValidatorType expect(Type expected)
    {
        auto actual = pop();
        VALIDATION_ASSERT(actual == expected);
        return actual;
    }

    void erase(uint32_t fromBegin, uint32_t fromEnd)
    {
        m_stack.erase(m_stack.begin() + fromBegin, m_stack.end() - fromEnd);
    }

    uint32_t size() const
    {
        return static_cast<uint32_t>(m_stack.size());
    }

    // FIXME: This needs a refactor and probably shouldn't be here
    ValidatorLabel& last_label()
    {
        assert(!labels.empty());
        return labels[labels.size() - 1];
    }

    std::vector<ValidatorLabel> labels;

private:
    std::vector<ValidatorType> m_stack;
};

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

        switch (exp.type)
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
        // if (i == 68)
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

void Validator::validate_function(const WasmFile::FunctionType& functionType, const WasmFile::Code& code)
{
    ValidatorValueStack stack;
    stack.labels.push_back(ValidatorLabel {
        .stackHeight = 0,
        .returnTypes = functionType.returns,
        .paramTypes = functionType.params,
        .type = ValidatorLabelType::Entry,
        .unreachable = false });

    std::vector<Type> locals;

    for (const auto param : functionType.params)
        locals.push_back(param);

    for (const auto local : code.locals)
        locals.push_back(local);

    auto validate_unary_operation_old = [&stack](Type type) {
        stack.expect(type);
        stack.push(type);
    };

    auto validate_unary_operation = [&stack](Type type, Type resultType) {
        stack.expect(type);
        stack.push(resultType);
    };

    auto validate_binary_operation_old = [&stack](Type type) {
        stack.expect(type);
        stack.expect(type);
        stack.push(type);
    };

    auto validate_binary_operation = [&stack](Type lhsType, Type rhsType, Type resultType) {
        stack.expect(rhsType);
        stack.expect(lhsType);
        stack.push(resultType);
    };

    auto validate_test_operation_old = [&stack](Type type) {
        stack.expect(type);
        stack.push(Type::i32);
    };

    auto validate_comparison_operation_old = [&stack](Type type) {
        stack.expect(type);
        stack.expect(type);
        stack.push(Type::i32);
    };

    auto validate_load_operation = [&stack, this](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        VALIDATION_ASSERT(memArg.memoryIndex < m_memories);
        VALIDATION_ASSERT((1ull << memArg.align) <= bitWidth / 8);
        stack.expect(Type::i32);
        stack.push(type);
    };

    auto validate_store_operation = [&stack, this](Type type, uint32_t bitWidth, const WasmFile::MemArg& memArg) {
        VALIDATION_ASSERT(memArg.memoryIndex < m_memories);
        VALIDATION_ASSERT((1ull << memArg.align) <= bitWidth / 8);
        stack.expect(type);
        stack.expect(Type::i32);
    };

    for (const auto& instruction : code.instructions)
    {
        switch (instruction.opcode)
        {
            using enum Opcode;
            case Opcode::unreachable:
                stack.last_label().unreachable = true;
                stack.erase(stack.last_label().stackHeight, 0);
                break;
            case Opcode::nop:
                break;
            case Opcode::block:
            case Opcode::loop: {
                const auto& arguments = instruction.get_arguments<BlockLoopArguments>();
                const auto& params = arguments.blockType.get_param_types(m_wasmFile);

                for (const auto type : std::views::reverse(params))
                    stack.expect(type);

                stack.labels.push_back(ValidatorLabel {
                    .stackHeight = stack.size(),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .type = instruction.opcode == Opcode::loop ? ValidatorLabelType::Loop : ValidatorLabelType::Block,
                    .unreachable = false });

                for (const auto type : params)
                    stack.push(type);

                break;
            }
            case Opcode::if_: {
                const auto& arguments = instruction.get_arguments<IfArguments>();
                const auto& params = arguments.blockType.get_param_types(m_wasmFile);

                stack.expect(Type::i32);

                // FIXME: This is duplicate with block
                for (const auto type : std::views::reverse(params))
                    stack.expect(type);

                stack.labels.push_back(ValidatorLabel {
                    .stackHeight = stack.size(),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .type = ValidatorLabelType::If,
                    .unreachable = false });

                for (const auto type : params)
                    stack.push(type);
                break;
            }
            case Opcode::else_: {
                auto& label = stack.last_label();
                VALIDATION_ASSERT(label.type == ValidatorLabelType::If);

                for (const auto type : std::views::reverse(label.returnTypes))
                    stack.expect(type);

                VALIDATION_ASSERT(stack.size() == label.stackHeight);

                for (const auto type : label.paramTypes)
                    stack.push(type);

                label.type = ValidatorLabelType::IfAfterElse;
                label.unreachable = false;
                break;
            }
            case Opcode::end: {
                auto& label = stack.last_label();

                if (label.type == ValidatorLabelType::If)
                    VALIDATION_ASSERT(label.returnTypes.size() == label.paramTypes.size());

                for (const auto type : std::views::reverse(label.returnTypes))
                    stack.expect(type);

                VALIDATION_ASSERT(stack.size() == label.stackHeight);

                for (const auto type : label.returnTypes)
                    stack.push(type);

                stack.labels.pop_back();
                break;
            }
            case Opcode::br: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() + 1 <= stack.labels.size());
                const auto& label = stack.labels[stack.labels.size() - instruction.get_arguments<uint32_t>() - 1];

                if (label.type != ValidatorLabelType::Loop)
                    for (const auto type : std::views::reverse(label.returnTypes))
                        stack.expect(type);

                stack.last_label().unreachable = true;
                stack.erase(stack.last_label().stackHeight, 0);
                break;
            }
            case Opcode::br_if: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() + 1 <= stack.labels.size());
                const auto& label = stack.labels[stack.labels.size() - instruction.get_arguments<uint32_t>() - 1];

                stack.expect(Type::i32);

                if (label.type != ValidatorLabelType::Loop)
                {
                    std::vector<ValidatorType> types;
                    for (const auto type : std::views::reverse(label.returnTypes))
                        types.push_back(stack.expect(type));

                    for (const auto type : types)
                        stack.push(type);
                }

                break;
            }
            case Opcode::br_table: {
                const auto& arguments = instruction.get_arguments<BranchTableArguments>();

                stack.expect(Type::i32);

                VALIDATION_ASSERT(arguments.defaultLabel + 1 <= stack.labels.size());
                for (const auto& labelIndex : arguments.labels)
                    VALIDATION_ASSERT(labelIndex + 1 <= stack.labels.size());

                stack.last_label().unreachable = true;
                stack.erase(stack.last_label().stackHeight, 0);
                break;
            }
            case Opcode::return_:
                for (const auto type : std::views::reverse(functionType.returns))
                    stack.expect(type);

                stack.last_label().unreachable = true;
                stack.erase(stack.last_label().stackHeight, 0);
                break;
            case Opcode::call: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_functions.size());

                const auto& calleeType = m_wasmFile->functionTypes[m_functions[instruction.get_arguments<uint32_t>()]];
                for (const auto type : std::views::reverse(calleeType.params))
                    stack.expect(type);

                for (const auto returned : calleeType.returns)
                    stack.push(returned);
                break;
            }
            case Opcode::call_indirect: {
                const auto& arguments = instruction.get_arguments<CallIndirectArguments>();

                stack.expect(Type::i32);

                VALIDATION_ASSERT(arguments.tableIndex < m_tables.size());
                VALIDATION_ASSERT(arguments.typeIndex < m_wasmFile->functionTypes.size());
                VALIDATION_ASSERT(m_tables[arguments.tableIndex] == Type::funcref);

                const auto& calleeType = m_wasmFile->functionTypes[arguments.typeIndex];
                for (const auto type : std::views::reverse(calleeType.params))
                    stack.expect(type);

                for (const auto returned : calleeType.returns)
                    stack.push(returned);
                break;
            }
            case Opcode::drop:
                stack.pop();
                break;
            case Opcode::select_: {
                stack.expect(Type::i32);

                auto a = stack.pop();
                auto b = stack.pop();
                stack.push(a.known() ? a : b);

                VALIDATION_ASSERT(a == b);
                VALIDATION_ASSERT(a == Type::i32 || a == Type::i64 || a == Type::f32 || a == Type::f64 || a == Type::v128);
                break;
            }
            case Opcode::select_typed: {
                // FIXME: This is duplicated with select
                const auto& arguments = std::get<std::vector<uint8_t>>(instruction.arguments);

                VALIDATION_ASSERT(arguments.size() == 1);
                VALIDATION_ASSERT(is_valid_type((Type)arguments[0]));

                stack.expect(Type::i32);

                auto a = stack.pop();
                auto b = stack.pop();
                stack.push((Type)arguments[0]);

                VALIDATION_ASSERT(a == (Type)arguments[0]);
                VALIDATION_ASSERT(b == (Type)arguments[0]);
                break;
            }
            case Opcode::local_get:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < locals.size());
                stack.push(locals[instruction.get_arguments<uint32_t>()]);
                break;
            case Opcode::local_set:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < locals.size());
                stack.expect(locals[instruction.get_arguments<uint32_t>()]);
                break;
            case Opcode::local_tee:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < locals.size());
                stack.expect(locals[instruction.get_arguments<uint32_t>()]);
                stack.push(locals[instruction.get_arguments<uint32_t>()]);
                break;
            case Opcode::global_get: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_globals.size());
                const auto& global = m_globals[instruction.get_arguments<uint32_t>()];
                stack.push(global.first);
                break;
            }
            case Opcode::global_set: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_globals.size());
                const auto& global = m_globals[instruction.get_arguments<uint32_t>()];
                VALIDATION_ASSERT(global.second == WasmFile::GlobalMutability::Variable);
                stack.expect(global.first);
                break;
            }
            case Opcode::table_get: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_tables.size());
                stack.expect(Type::i32);
                stack.push(m_tables[instruction.get_arguments<uint32_t>()]);
                break;
            }
            case Opcode::table_set: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_tables.size());
                stack.expect(m_tables[instruction.get_arguments<uint32_t>()]);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::i32_load:
                validate_load_operation(Type::i32, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_load:
                validate_load_operation(Type::i64, 64, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::f32_load:
                validate_load_operation(Type::f32, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::f64_load:
                validate_load_operation(Type::f64, 64, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i32_load8_s:
            case Opcode::i32_load8_u:
                validate_load_operation(Type::i32, 8, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i32_load16_s:
            case Opcode::i32_load16_u:
                validate_load_operation(Type::i32, 16, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_load8_s:
            case Opcode::i64_load8_u:
                validate_load_operation(Type::i64, 8, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_load16_s:
            case Opcode::i64_load16_u:
                validate_load_operation(Type::i64, 16, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_load32_s:
            case Opcode::i64_load32_u:
                validate_load_operation(Type::i64, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i32_store:
                validate_store_operation(Type::i32, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_store:
                validate_store_operation(Type::i64, 64, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::f32_store:
                validate_store_operation(Type::f32, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::f64_store:
                validate_store_operation(Type::f64, 64, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i32_store8:
                validate_store_operation(Type::i32, 8, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i32_store16:
                validate_store_operation(Type::i32, 16, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_store8:
                validate_store_operation(Type::i64, 8, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_store16:
                validate_store_operation(Type::i64, 16, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::i64_store32:
                validate_store_operation(Type::i64, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::memory_size:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_memories);
                stack.push(Type::i32);
                break;
            case Opcode::memory_grow:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_memories);
                stack.expect(Type::i32);
                stack.push(Type::i32);
                break;
            case Opcode::i32_const:
                stack.push(Type::i32);
                break;
            case Opcode::i64_const:
                stack.push(Type::i64);
                break;
            case Opcode::f32_const:
                stack.push(Type::f32);
                break;
            case Opcode::f64_const:
                stack.push(Type::f64);
                break;

#define X(opcode, operation, type, resultType)                                                                        \
    case opcode:                                                                                                      \
        validate_unary_operation(type_from_cpp_type<ToValueType<type>>, type_from_cpp_type<ToValueType<resultType>>); \
        break;
                ENUMERATE_UNARY_OPERATIONS(X)
#undef X

#define X(opcode, operation, lhsType, rhsType, resultType)                                                                                                          \
    case opcode:                                                                                                                                                    \
        validate_binary_operation(type_from_cpp_type<ToValueType<lhsType>>, type_from_cpp_type<ToValueType<rhsType>>, type_from_cpp_type<ToValueType<resultType>>); \
        break;
                ENUMERATE_BINARY_OPERATIONS(X)
#undef X

            case Opcode::ref_null:
                stack.push(instruction.get_arguments<Type>());
                break;
            case Opcode::ref_is_null:
                VALIDATION_ASSERT(stack.pop().is_reference_type());
                stack.push(Type::i32);
                break;

            case Opcode::ref_func:
                // TODO: Check if the function was declared
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_functions.size());
                stack.push(Type::funcref);
                break;
            case Opcode::memory_init: {
                VALIDATION_ASSERT(m_wasmFile->dataCount);
                const auto& arguments = instruction.get_arguments<MemoryInitArguments>();
                VALIDATION_ASSERT(arguments.memoryIndex < m_memories);
                VALIDATION_ASSERT(arguments.dataIndex < m_wasmFile->dataBlocks.size());
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::data_drop:
                VALIDATION_ASSERT(m_wasmFile->dataCount);
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_wasmFile->dataBlocks.size());
                break;
            case Opcode::memory_copy: {
                const auto& arguments = instruction.get_arguments<MemoryCopyArguments>();
                VALIDATION_ASSERT(arguments.destination < m_memories);
                VALIDATION_ASSERT(arguments.source < m_memories);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::memory_fill:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_memories);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                break;
            case Opcode::table_init: {
                const auto& arguments = instruction.get_arguments<TableInitArguments>();
                VALIDATION_ASSERT(arguments.tableIndex < m_tables.size());
                VALIDATION_ASSERT(arguments.elementIndex < m_wasmFile->elements.size());
                VALIDATION_ASSERT(m_tables[arguments.tableIndex] == m_wasmFile->elements[arguments.elementIndex].valueType);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::elem_drop:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_wasmFile->elements.size());
                break;
            case Opcode::table_copy: {
                const auto& arguments = instruction.get_arguments<TableCopyArguments>();
                VALIDATION_ASSERT(arguments.destination < m_tables.size());
                VALIDATION_ASSERT(arguments.source < m_tables.size());
                VALIDATION_ASSERT(m_tables[arguments.destination] == m_tables[arguments.source]);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::table_grow:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_tables.size());
                stack.expect(Type::i32);
                stack.expect(m_tables[instruction.get_arguments<uint32_t>()]);
                stack.push(Type::i32);
                break;
            case Opcode::table_size:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_tables.size());
                stack.push(Type::i32);
                break;
            case Opcode::table_fill:
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_tables.size());
                stack.expect(Type::i32);
                stack.expect(m_tables[instruction.get_arguments<uint32_t>()]);
                stack.expect(Type::i32);
                break;
            case Opcode::v128_load:
            case Opcode::v128_load8x8_s:
            case Opcode::v128_load8x8_u:
            case Opcode::v128_load16x4_s:
            case Opcode::v128_load16x4_u:
            case Opcode::v128_load32x2_s:
            case Opcode::v128_load32x2_u:
            case Opcode::v128_load8_splat:
            case Opcode::v128_load16_splat:
            case Opcode::v128_load32_splat:
            case Opcode::v128_load64_splat:
            case Opcode::v128_load32_zero:
            case Opcode::v128_load64_zero:
                validate_load_operation(Type::v128, 128, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load8_lane:
            case Opcode::v128_load16_lane:
            case Opcode::v128_load32_lane:
            case Opcode::v128_load64_lane: {
                const auto& arguments = instruction.get_arguments<LoadStoreLaneArguments>();
                VALIDATION_ASSERT(arguments.memArg.memoryIndex < m_memories);
                VALIDATION_ASSERT((1ull << arguments.memArg.align) <= 128 / 8);
                stack.expect(Type::v128);
                stack.expect(Type::i32);
                stack.push(Type::v128);
                break;
            }
            case Opcode::v128_store:
                validate_store_operation(Type::v128, 128, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_store8_lane:
            case Opcode::v128_store16_lane:
            case Opcode::v128_store32_lane:
            case Opcode::v128_store64_lane: {
                const auto& arguments = instruction.get_arguments<LoadStoreLaneArguments>();
                VALIDATION_ASSERT(arguments.memArg.memoryIndex < m_memories);
                VALIDATION_ASSERT((1ull << arguments.memArg.align) <= 128 / 8);
                stack.expect(Type::v128);
                stack.expect(Type::i32);
                break;
            }
            case Opcode::v128_const:
                stack.push(Type::v128);
                break;
            case Opcode::i8x16_shuffle:
                // FIXME: Check lanes
                validate_binary_operation_old(Type::v128);
                break;
            case Opcode::i8x16_splat:
            case Opcode::i16x8_splat:
            case Opcode::i32x4_splat:
                stack.expect(Type::i32);
                stack.push(Type::v128);
                break;
            case Opcode::i64x2_splat:
                stack.expect(Type::i64);
                stack.push(Type::v128);
                break;
            case Opcode::f32x4_splat:
                stack.expect(Type::f32);
                stack.push(Type::v128);
                break;
            case Opcode::f64x2_splat:
                stack.expect(Type::f64);
                stack.push(Type::v128);
                break;
            case Opcode::i8x16_extract_lane_s:
            case Opcode::i8x16_extract_lane_u:
            case Opcode::i16x8_extract_lane_s:
            case Opcode::i16x8_extract_lane_u:
            case Opcode::i32x4_extract_lane:
                stack.expect(Type::v128);
                stack.push(Type::i32);
                break;
            case Opcode::i64x2_extract_lane:
                stack.expect(Type::v128);
                stack.push(Type::i64);
                break;
            case Opcode::f32x4_extract_lane:
                stack.expect(Type::v128);
                stack.push(Type::f32);
                break;
            case Opcode::f64x2_extract_lane:
                stack.expect(Type::v128);
                stack.push(Type::f64);
                break;
            case Opcode::i8x16_replace_lane:
            case Opcode::i16x8_replace_lane:
            case Opcode::i32x4_replace_lane:
                stack.expect(Type::i32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::i64x2_replace_lane:
                stack.expect(Type::i64);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::f32x4_replace_lane:
                stack.expect(Type::f32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::f64x2_replace_lane:
                stack.expect(Type::f64);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::i8x16_all_true:
            case Opcode::i8x16_bitmask:
            case Opcode::i16x8_all_true:
            case Opcode::i16x8_bitmask:
            case Opcode::i32x4_all_true:
            case Opcode::i32x4_bitmask:
            case Opcode::i64x2_all_true:
            case Opcode::i64x2_bitmask:
            case Opcode::v128_any_true:
                validate_test_operation_old(Type::v128);
                break;
            case Opcode::i8x16_swizzle:
            case Opcode::i8x16_eq:
            case Opcode::i8x16_ne:
            case Opcode::i8x16_lt_s:
            case Opcode::i8x16_lt_u:
            case Opcode::i8x16_gt_s:
            case Opcode::i8x16_gt_u:
            case Opcode::i8x16_le_s:
            case Opcode::i8x16_le_u:
            case Opcode::i8x16_ge_s:
            case Opcode::i8x16_ge_u:
            case Opcode::i16x8_eq:
            case Opcode::i16x8_ne:
            case Opcode::i16x8_lt_s:
            case Opcode::i16x8_lt_u:
            case Opcode::i16x8_gt_s:
            case Opcode::i16x8_gt_u:
            case Opcode::i16x8_le_s:
            case Opcode::i16x8_le_u:
            case Opcode::i16x8_ge_s:
            case Opcode::i16x8_ge_u:
            case Opcode::i32x4_eq:
            case Opcode::i32x4_ne:
            case Opcode::i32x4_lt_s:
            case Opcode::i32x4_lt_u:
            case Opcode::i32x4_gt_s:
            case Opcode::i32x4_gt_u:
            case Opcode::i32x4_le_s:
            case Opcode::i32x4_le_u:
            case Opcode::i32x4_ge_s:
            case Opcode::i32x4_ge_u:
            case Opcode::f32x4_eq:
            case Opcode::f32x4_ne:
            case Opcode::f32x4_lt:
            case Opcode::f32x4_gt:
            case Opcode::f32x4_le:
            case Opcode::f32x4_ge:
            case Opcode::f64x2_eq:
            case Opcode::f64x2_ne:
            case Opcode::f64x2_lt:
            case Opcode::f64x2_gt:
            case Opcode::f64x2_le:
            case Opcode::f64x2_ge:
            case Opcode::i64x2_eq:
            case Opcode::i64x2_ne:
            case Opcode::i64x2_lt_s:
            case Opcode::i64x2_gt_s:
            case Opcode::i64x2_le_s:
            case Opcode::i64x2_ge_s:
            case Opcode::v128_and:
            case Opcode::v128_andnot:
            case Opcode::v128_or:
            case Opcode::v128_xor:
            case Opcode::i8x16_add:
            case Opcode::i8x16_add_sat_s:
            case Opcode::i8x16_add_sat_u:
            case Opcode::i8x16_sub:
            case Opcode::i8x16_sub_sat_s:
            case Opcode::i8x16_sub_sat_u:
            case Opcode::i8x16_min_s:
            case Opcode::i8x16_min_u:
            case Opcode::i8x16_max_s:
            case Opcode::i8x16_max_u:
            case Opcode::i8x16_avgr_u:
            case Opcode::i16x8_q15mulr_sat_s:
            case Opcode::i16x8_add:
            case Opcode::i16x8_add_sat_s:
            case Opcode::i16x8_add_sat_u:
            case Opcode::i16x8_sub:
            case Opcode::i16x8_sub_sat_s:
            case Opcode::i16x8_sub_sat_u:
            case Opcode::i16x8_mul:
            case Opcode::i16x8_min_s:
            case Opcode::i16x8_min_u:
            case Opcode::i16x8_max_s:
            case Opcode::i16x8_max_u:
            case Opcode::i16x8_avgr_u:
            case Opcode::i32x4_add:
            case Opcode::i32x4_sub:
            case Opcode::i32x4_mul:
            case Opcode::i32x4_min_s:
            case Opcode::i32x4_min_u:
            case Opcode::i32x4_max_s:
            case Opcode::i32x4_max_u:
            case Opcode::i64x2_add:
            case Opcode::i64x2_sub:
            case Opcode::i64x2_mul:
            case Opcode::f32x4_add:
            case Opcode::f32x4_sub:
            case Opcode::f32x4_mul:
            case Opcode::f32x4_div:
            case Opcode::f32x4_min:
            case Opcode::f32x4_max:
            case Opcode::f32x4_pmin:
            case Opcode::f32x4_pmax:
            case Opcode::f64x2_add:
            case Opcode::f64x2_sub:
            case Opcode::f64x2_mul:
            case Opcode::f64x2_div:
            case Opcode::f64x2_min:
            case Opcode::f64x2_max:
            case Opcode::f64x2_pmin:
            case Opcode::f64x2_pmax:
            case Opcode::i8x16_narrow_i16x8_s:
            case Opcode::i8x16_narrow_i16x8_u:
            case Opcode::i16x8_extmul_low_i8x16_s:
            case Opcode::i16x8_extmul_high_i8x16_s:
            case Opcode::i16x8_extmul_low_i8x16_u:
            case Opcode::i16x8_extmul_high_i8x16_u:
            case Opcode::i32x4_dot_i16x8_s:
            case Opcode::i32x4_extmul_low_i16x8_s:
            case Opcode::i32x4_extmul_high_i16x8_s:
            case Opcode::i32x4_extmul_low_i16x8_u:
            case Opcode::i32x4_extmul_high_i16x8_u:
            case Opcode::i64x2_extmul_low_i32x4_s:
            case Opcode::i64x2_extmul_high_i32x4_s:
            case Opcode::i64x2_extmul_low_i32x4_u:
            case Opcode::i64x2_extmul_high_i32x4_u:
            case Opcode::i16x8_narrow_i32x4_s:
            case Opcode::i16x8_narrow_i32x4_u:
                validate_binary_operation_old(Type::v128);
                break;
            case Opcode::i8x16_shl:
            case Opcode::i8x16_shr_s:
            case Opcode::i8x16_shr_u:
            case Opcode::i16x8_shl:
            case Opcode::i16x8_shr_s:
            case Opcode::i16x8_shr_u:
            case Opcode::i32x4_shl:
            case Opcode::i32x4_shr_s:
            case Opcode::i32x4_shr_u:
            case Opcode::i64x2_shl:
            case Opcode::i64x2_shr_s:
            case Opcode::i64x2_shr_u:
                stack.expect(Type::i32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::v128_not:
            case Opcode::i8x16_abs:
            case Opcode::i8x16_neg:
            case Opcode::i8x16_popcnt:
            case Opcode::f32x4_ceil:
            case Opcode::f32x4_floor:
            case Opcode::f32x4_trunc:
            case Opcode::f32x4_nearest:
            case Opcode::f64x2_ceil:
            case Opcode::f64x2_floor:
            case Opcode::f64x2_trunc:
            case Opcode::i16x8_abs:
            case Opcode::i16x8_neg:
            case Opcode::i16x8_extend_low_i8x16_s:
            case Opcode::i16x8_extend_high_i8x16_s:
            case Opcode::i16x8_extend_low_i8x16_u:
            case Opcode::i16x8_extend_high_i8x16_u:
            case Opcode::f64x2_nearest:
            case Opcode::i32x4_abs:
            case Opcode::i32x4_neg:
            case Opcode::i64x2_abs:
            case Opcode::i64x2_neg:
            case Opcode::f32x4_abs:
            case Opcode::f32x4_neg:
            case Opcode::f32x4_sqrt:
            case Opcode::f64x2_abs:
            case Opcode::f64x2_neg:
            case Opcode::f64x2_sqrt:
            case Opcode::f32x4_demote_f64x2_zero:
            case Opcode::f64x2_promote_low_f32x4:
            case Opcode::i16x8_extadd_pairwise_i8x16_s:
            case Opcode::i16x8_extadd_pairwise_i8x16_u:
            case Opcode::i32x4_extadd_pairwise_i16x8_s:
            case Opcode::i32x4_extadd_pairwise_i16x8_u:
            case Opcode::i32x4_extend_low_i16x8_s:
            case Opcode::i32x4_extend_high_i16x8_s:
            case Opcode::i32x4_extend_low_i16x8_u:
            case Opcode::i32x4_extend_high_i16x8_u:
            case Opcode::i64x2_extend_low_i32x4_s:
            case Opcode::i64x2_extend_high_i32x4_s:
            case Opcode::i64x2_extend_low_i32x4_u:
            case Opcode::i64x2_extend_high_i32x4_u:
            case Opcode::i32x4_trunc_sat_f32x4_s:
            case Opcode::i32x4_trunc_sat_f32x4_u:
            case Opcode::f32x4_convert_i32x4_s:
            case Opcode::f32x4_convert_i32x4_u:
            case Opcode::i32x4_trunc_sat_f64x2_s_zero:
            case Opcode::i32x4_trunc_sat_f64x2_u_zero:
            case Opcode::f64x2_convert_low_i32x4_s:
            case Opcode::f64x2_convert_low_i32x4_u:
                validate_unary_operation_old(Type::v128);
                break;
            case Opcode::v128_bitselect:
                stack.expect(Type::v128);
                stack.expect(Type::v128);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            default:
                std::println(std::cerr, "Error: No validation for opcode {:#x}", static_cast<uint32_t>(instruction.opcode));
                VALIDATION_ASSERT(false);
        }
    }

    // for (const auto type : std::views::reverse(functionType.returns))
    //     stack.expect(type);

    // VALIDATION_ASSERT(stack.size() == 0);
}

void Validator::validate_constant_expression(const std::vector<Instruction>& instructions, Type expectedReturnType, bool globalRestrictions)
{
    ValidatorValueStack stack;
    stack.labels.push_back(ValidatorLabel {
        .stackHeight = 0,
        .returnTypes = { expectedReturnType },
        .paramTypes = {},
        .type = ValidatorLabelType::Entry,
        .unreachable = false });

    for (size_t ip = 0; ip < instructions.size(); ip++)
    {
        const auto& instruction = instructions[ip];
        switch (instruction.opcode)
        {
            case Opcode::end:
                VALIDATION_ASSERT(ip == instructions.size() - 1);
                break;
            case Opcode::global_get: {
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_globals.size());
                if (globalRestrictions)
                    VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_imported_global_count);
                const auto& global = m_globals[instruction.get_arguments<uint32_t>()];
                VALIDATION_ASSERT(global.second == WasmFile::GlobalMutability::Constant);
                stack.push(global.first);
                break;
            }
            case Opcode::i32_const:
                stack.push(Type::i32);
                break;
            case Opcode::i64_const:
                stack.push(Type::i64);
                break;
            case Opcode::f32_const:
                stack.push(Type::f32);
                break;
            case Opcode::f64_const:
                stack.push(Type::f64);
                break;
            case Opcode::ref_null:
                stack.push(instruction.get_arguments<Type>());
                break;
            case Opcode::ref_func:
                // TODO: Check if the function was declared
                VALIDATION_ASSERT(instruction.get_arguments<uint32_t>() < m_functions.size());
                stack.push(Type::funcref);
                break;
            case Opcode::v128_const:
                stack.push(Type::v128);
                break;
            default:
                VALIDATION_ASSERT(false);
        }
    }

    stack.expect(expectedReturnType);
    VALIDATION_ASSERT(stack.size() == 0);
}
