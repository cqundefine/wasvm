#include <Parser.h>
#include <SIMD.h>
#include <Type.h>
#include <Util.h>
#include <Validator.h>
#include <Value.h>
#include <ValueStack.h>
#include <WasmFile.h>
#include <cassert>
#include <cstdint>
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
    Label label;
};

class ValidatorStack
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

    void push_label(const ValidatorLabel& label)
    {
        m_labels.push_back(std::move(label));
    }

    void pop_label()
    {
        assert(!m_labels.empty());
        m_labels.pop_back();
    }

    ValidatorLabel get_label(uint32_t index)
    {
        VALIDATION_ASSERT(index + 1 <= m_labels.size());
        return m_labels[m_labels.size() - index - 1];
    }

    ValidatorLabel& last_label()
    {
        assert(!m_labels.empty());
        return m_labels[m_labels.size() - 1];
    }

private:
    std::vector<ValidatorType> m_stack;
    std::vector<ValidatorLabel> m_labels;
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

        /*if (element.mode == WasmFile::ElementMode::Declarative)
        {
            for (size_t i = 0; i < element.functionIndexes.size(); i++)
            {
                if (element.functionIndexes.empty())
                {
                    Value referenceValue = run_global_restricted_constant_expression(element.referencesExpr[i]);
                    assert(referenceValue.holds_alternative<Reference>());
                    const auto reference = referenceValue.get<Reference>();
                    VALIDATION_ASSERT(reference.type == ReferenceType::Function);
                    m_declared_functions.push_back(reference.index);
                }
                else
                {
                    m_declared_functions.push_back(element.functionIndexes[i]);
                }
            }
        }*/
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

void Validator::validate_function(const WasmFile::FunctionType& functionType, WasmFile::Code& code)
{
    ValidatorStack stack;
    stack.push_label(ValidatorLabel {
        .stackHeight = 0,
        .returnTypes = functionType.returns,
        .paramTypes = functionType.params,
        .type = ValidatorLabelType::Entry,
        .unreachable = false,
        .label = Label {
            .continuation = static_cast<uint32_t>(code.instructions.size()),
            .arity = static_cast<uint32_t>(functionType.returns.size()),
            .stackHeight = 0 } });

    std::vector<Type> locals;

    for (const auto param : functionType.params)
        locals.push_back(param);

    for (const auto local : code.locals)
        locals.push_back(local);

    auto validate_unary_operation = [&stack](Type type, Type resultType) {
        stack.expect(type);
        stack.push(resultType);
    };

    auto validate_binary_operation = [&stack](Type lhsType, Type rhsType, Type resultType) {
        stack.expect(rhsType);
        stack.expect(lhsType);
        stack.push(resultType);
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

    auto validate_load_lane_operation = [&stack, this](Type type, uint32_t laneSize, const LoadStoreLaneArguments& arguments) {
        VALIDATION_ASSERT(arguments.memArg.memoryIndex < m_memories);
        VALIDATION_ASSERT((1ull << arguments.memArg.align) <= laneSize / 8);
        VALIDATION_ASSERT(arguments.lane < 128 / laneSize);
        stack.expect(Type::v128);
        stack.expect(Type::i32);
        stack.push(Type::v128);
    };

    for (auto& instruction : code.instructions)
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
                instruction.arguments = {};

                Label label = arguments.label;
                label.stackHeight = static_cast<uint32_t>(stack.size() - arguments.blockType.get_param_types(m_wasmFile).size());

                for (const auto type : std::views::reverse(params))
                    stack.expect(type);

                stack.push_label(ValidatorLabel {
                    .stackHeight = stack.size(),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .type = instruction.opcode == Opcode::loop ? ValidatorLabelType::Loop : ValidatorLabelType::Block,
                    .unreachable = false,
                    .label = label });

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

                stack.push_label(ValidatorLabel {
                    .stackHeight = stack.size(),
                    .returnTypes = arguments.blockType.get_return_types(m_wasmFile),
                    .paramTypes = params,
                    .type = ValidatorLabelType::If,
                    .unreachable = false,
                    .label = arguments.endLabel });

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

                stack.pop_label();
                break;
            }
            case Opcode::br: {
                const auto& label = stack.get_label(instruction.get_arguments<uint32_t>());
                instruction.arguments = label.label;

                if (label.type != ValidatorLabelType::Loop)
                    for (const auto type : std::views::reverse(label.returnTypes))
                        stack.expect(type);

                stack.last_label().unreachable = true;
                stack.erase(stack.last_label().stackHeight, 0);
                break;
            }
            case Opcode::br_if: {
                const auto& label = stack.get_label(instruction.get_arguments<uint32_t>());
                instruction.arguments = label.label;

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
                const auto& arguments = instruction.get_arguments<BranchTableArgumentsPrevalidated>();

                stack.expect(Type::i32);

                const auto& defaultLabel = stack.get_label(arguments.defaultLabel);

                std::vector<Label> labels;
                for (const auto& labelIndex : arguments.labels)
                    labels.push_back(stack.get_label(labelIndex).label);

                instruction.arguments = BranchTableArguments {
                    .labels = std::move(labels),
                    .defaultLabel = defaultLabel.label
                };

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

#define X(opcode, memoryType, targetType)                                                                                                            \
    case opcode:                                                                                                                                     \
        validate_load_operation(type_from_cpp_type<ToValueType<targetType>>, sizeof(memoryType) * 8, instruction.get_arguments<WasmFile::MemArg>()); \
        break;
                ENUMERATE_LOAD_OPERATIONS(X)
#undef X

#define X(opcode, memoryType, targetType)                                                                                                             \
    case opcode:                                                                                                                                      \
        validate_store_operation(type_from_cpp_type<ToValueType<targetType>>, sizeof(memoryType) * 8, instruction.get_arguments<WasmFile::MemArg>()); \
        break;
                ENUMERATE_STORE_OPERATIONS(X)
#undef X

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
                // VALIDATION_ASSERT(vector_contains(m_declared_functions, instruction.get_arguments<uint32_t>()));
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
            case Opcode::v128_load8_splat:
                validate_load_operation(Type::v128, 8, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load16_splat:
                validate_load_operation(Type::v128, 16, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load32_splat:
            case Opcode::v128_load32_zero:
                validate_load_operation(Type::v128, 32, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load64_splat:
            case Opcode::v128_load64_zero:
                validate_load_operation(Type::v128, 64, instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load8_lane:
                validate_load_lane_operation(Type::v128, 8, instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case Opcode::v128_load16_lane:
                validate_load_lane_operation(Type::v128, 16, instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case Opcode::v128_load32_lane:
                validate_load_lane_operation(Type::v128, 32, instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case Opcode::v128_load64_lane:
                validate_load_lane_operation(Type::v128, 64, instruction.get_arguments<LoadStoreLaneArguments>());
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
            case Opcode::i8x16_shuffle: {
                const auto& lanes = instruction.get_arguments<uint8x16_t>();
                for (uint8_t i = 0; i < 16; i++)
                    VALIDATION_ASSERT(lanes[i] < 32);
                validate_binary_operation(Type::v128, Type::v128, Type::v128);
                break;
            }
            case Opcode::i8x16_extract_lane_s:
            case Opcode::i8x16_extract_lane_u: {
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 16);
                stack.expect(Type::v128);
                stack.push(Type::i32);
                break;
            }
            case Opcode::i16x8_extract_lane_s:
            case Opcode::i16x8_extract_lane_u:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 8);
                stack.expect(Type::v128);
                stack.push(Type::i32);
                break;
            case Opcode::i32x4_extract_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 4);
                stack.expect(Type::v128);
                stack.push(Type::i32);
                break;
            case Opcode::i64x2_extract_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 2);
                stack.expect(Type::v128);
                stack.push(Type::i64);
                break;
            case Opcode::f32x4_extract_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 4);
                stack.expect(Type::v128);
                stack.push(Type::f32);
                break;
            case Opcode::f64x2_extract_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 2);
                stack.expect(Type::v128);
                stack.push(Type::f64);
                break;
            case Opcode::i8x16_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 16);
                stack.expect(Type::i32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::i16x8_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 8);
                stack.expect(Type::i32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::i32x4_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 4);
                stack.expect(Type::i32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::i64x2_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 2);
                stack.expect(Type::i64);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::f32x4_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 4);
                stack.expect(Type::f32);
                stack.expect(Type::v128);
                stack.push(Type::v128);
                break;
            case Opcode::f64x2_replace_lane:
                VALIDATION_ASSERT(instruction.get_arguments<uint8_t>() < 2);
                stack.expect(Type::f64);
                stack.expect(Type::v128);
                stack.push(Type::v128);
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
    ValidatorStack stack;
    // TODO: This is a hack to make the validator work with the current code
    stack.push_label(ValidatorLabel {});

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
                // VALIDATION_ASSERT(vector_contains(m_declared_functions, instruction.get_arguments<uint32_t>()));
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

Value Validator::run_global_restricted_constant_expression(const std::vector<Instruction>& instructions)
{
    ValueStack stack;

    for (const auto& instruction : instructions)
    {
        switch (instruction.opcode)
        {
            using enum Opcode;
            case end:
                break;
            case i32_const:
                stack.push(instruction.get_arguments<uint32_t>());
                break;
            case i64_const:
                stack.push(instruction.get_arguments<uint64_t>());
                break;
            case f32_const:
                stack.push(instruction.get_arguments<float>());
                break;
            case f64_const:
                stack.push(instruction.get_arguments<double>());
                break;
            case ref_null:
                stack.push(default_value_for_type(instruction.get_arguments<Type>()));
                break;
            case ref_func:
                stack.push(Reference { ReferenceType::Function, instruction.get_arguments<uint32_t>() });
                break;
            case v128_const:
                stack.push(instruction.get_arguments<uint128_t>());
                break;
            default:
                assert(false);
        }
    }

    assert(stack.size() == 1);
    return stack.pop();
}
