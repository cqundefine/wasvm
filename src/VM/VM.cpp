#include "VM.h"
#include "Operators.h"
#include "Util/Util.h"
#include "VM/Module.h"
#include "VM/Type.h"
#include "VM/Value.h"
#include "WasmFile/Opcode.h"
#include "WasmFile/Validator.h"
#include <cassert>
#include <cstring>
#include <utility>

Ref<RealModule> VM::load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current)
{
    auto new_module = MakeRef<RealModule>(m_next_module_id++, file);

    for (const auto& import : new_module->wasm_file()->imports)
    {
        ImportLocation location = find_import(import.environment, import.name, import.type);
        switch (import.type)
        {
            case WasmFile::ImportType::Function: {
                const auto function = std::get<Ref<Function>>(location.imported);
                if (function->type() != file->functionTypes[import.functionTypeIndex])
                    throw Trap("Invalid function import");
                new_module->add_function(function);
                break;
            }
            case WasmFile::ImportType::Table: {
                const auto table = std::get<Ref<Table>>(location.imported);
                if (table->type() != import.tableRefType || !table->limits().fits_within(import.tableLimits))
                    throw Trap("Invalid table import");
                new_module->add_table(table);
                break;
            }
            case WasmFile::ImportType::Memory: {
                const auto memory = std::get<Ref<Memory>>(location.imported);
                if (!memory->limits().fits_within(import.memoryLimits))
                    throw Trap("Invalid memory import");
                new_module->add_memory(memory);
                break;
            }
            case WasmFile::ImportType::Global: {
                const auto global = std::get<Ref<Global>>(location.imported);
                if (global->type() != import.globalType || global->mutability() != import.globalMutability)
                    throw Trap("Invalid global import");
                new_module->add_global(global);
                break;
            }
        }
    }

    for (size_t i = 0; i < new_module->wasm_file()->functionTypeIndexes.size(); i++)
    {
        auto* type = &new_module->wasm_file()->functionTypes[new_module->wasm_file()->functionTypeIndexes[i]];
        auto* code = &new_module->wasm_file()->codeBlocks[i];
        auto function = MakeRef<RealFunction>(type, code, new_module);
        new_module->add_function(function);
    }

    for (const auto& global : new_module->wasm_file()->globals)
        new_module->add_global(MakeRef<Global>(global.type, global.mutability, run_bare_code(new_module, global.initCode)));

    for (const auto& memory : new_module->wasm_file()->memories)
        new_module->add_memory(MakeRef<Memory>(memory));

    for (const auto& tableInfo : new_module->wasm_file()->tables)
        new_module->add_table(MakeRef<Table>(tableInfo, Reference { get_reference_type_from_reftype(tableInfo.refType), {}, new_module.get() }));

    for (auto& element : new_module->wasm_file()->elements)
    {
        if (element.mode == WasmFile::ElementMode::Active)
        {
            const auto table = new_module->get_table(element.table);

            Value beginValue = run_bare_code(new_module, element.expr);
            uint64_t begin = table->address_type() == AddressType::i64 ? beginValue.get<uint64_t>() : beginValue.get<uint32_t>();

            size_t size = element.functionIndexes.empty() ? element.referencesExpr.size() : element.functionIndexes.size();

            if (begin + size > table->size())
                throw Trap("Out of bounds element");

            for (size_t i = 0; i < size; i++)
            {
                if (element.functionIndexes.empty())
                {
                    Value reference = run_bare_code(new_module, element.referencesExpr[i]);
                    table->set(begin + i, reference.get<Reference>());
                }
                else
                {
                    table->set(begin + i, Reference { ReferenceType::Function, element.functionIndexes[i], new_module.get() });
                }
            }
        }

        if (element.mode == WasmFile::ElementMode::Active || element.mode == WasmFile::ElementMode::Declarative)
            element = WasmFile::Element();
    }

    for (auto& data : new_module->wasm_file()->dataBlocks)
    {
        if (data.mode == WasmFile::ElementMode::Active)
        {
            const auto* memory = new_module->get_memory(data.memoryIndex);

            Value beginValue = run_bare_code(new_module, data.expr);
            uint64_t begin = memory->address_type() == AddressType::i64 ? beginValue.get<uint64_t>() : beginValue.get<uint32_t>();

            if (memory->check_outside_bounds(begin, data.data.size()))
                throw Trap("Out of bounds data");

            memcpy(memory->data() + begin, data.data.data(), data.data.size());

            data = WasmFile::Data();
        }
    }

    if (auto start_function = new_module->start_function(); start_function.has_value())
        (void)start_function.value()->run({});

    if (!dont_make_current)
        m_current_module = new_module;

    return new_module;
}

void VM::register_module(const std::string& name, Ref<Module> module)
{
    m_registered_modules[name] = module;
}

std::vector<Value> VM::run_function(const std::string& name, std::span<const Value> args)
{
    return run_function(m_current_module, name, args);
}

std::vector<Value> VM::run_function(const std::string& mod, const std::string& name, std::span<const Value> args)
{
    return run_function(m_registered_modules[mod], name, args);
}

std::vector<Value> VM::run_function(Ref<Module> mod, const std::string& name, std::span<const Value> args)
{
    const auto maybeFunction = mod->try_import(name, WasmFile::ImportType::Function);
    if (!maybeFunction.has_value())
        throw Trap(std::format("Unknown function: {}", name));
    return std::get<Ref<Function>>(maybeFunction.value())->run(args);
}

std::vector<Value> VM::run_function(Ref<RealModule> mod, const RealFunction* function, std::span<const Value> args)
{
    if (m_frame_stack.size() >= MAX_FRAME_STACK_SIZE)
        throw Trap("Frame stack exceeded");

    m_frame_stack.push(m_frame);
    m_frame = new Frame(mod);

    DEFER(clean_up_frame());

    const auto& type = function->type();
    const auto& code = function->code();

    if (args.size() != type.params.size())
        throw Trap("Invalid argument count passed");

    for (const auto& param : args)
        m_frame->locals.push_back(param);

    for (const auto local : code.locals)
        m_frame->locals.push_back(default_value_for_type(local));

    while (m_frame->ip < code.instructions.size())
    {
        const auto& instruction = code.instructions[m_frame->ip++];

        switch (instruction.opcode)
        {
            using enum Opcode;
            case unreachable:
                throw Trap("Unreachable");
            case nop:
            case block:
            case loop:
                break;
            case if_: {
                const auto& arguments = instruction.get_arguments<IfArguments>();

                uint32_t value = m_frame->stack.pop_as<uint32_t>();

                if (value == 0)
                {
                    if (arguments.elseLocation.has_value())
                        m_frame->ip = arguments.elseLocation.value() + 1;
                    else
                        m_frame->ip = arguments.endLabel.continuation;
                }
                break;
            }
            case else_:
                m_frame->ip = instruction.get_arguments<Label>().continuation;
                break;
            case end:
                break;

            case br:
                branch_to_label(instruction.get_arguments<Label>());
                break;
            case br_if:
                if (m_frame->stack.pop_as<uint32_t>() != 0)
                    branch_to_label(instruction.get_arguments<Label>());
                break;
            case br_table: {
                const auto& arguments = instruction.get_arguments<BranchTableArguments>();
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                if (index < arguments.labels.size())
                    branch_to_label(arguments.labels[index]);
                else
                    branch_to_label(arguments.defaultLabel);
                break;
            }

            case return_:
                return std::move(m_frame->stack.pop_n_values(type.returns.size()));
            case call:
                call_function(mod->get_function(instruction.get_arguments<uint32_t>()));
                break;
            case call_indirect: {
                const auto& arguments = instruction.get_arguments<CallIndirectArguments>();

                const auto* table = mod->get_table(arguments.tableIndex);
                uint64_t index = pop_address(table);

                const auto reference = table->get(index);

                if (!reference.index)
                    throw Trap("Call indirect on null reference");

                if (reference.type != ReferenceType::Function)
                    throw Trap("Call indirect on non-function reference");

                auto* module = reference.module ? reference.module : mod.get();
                auto function = module->get_function(*reference.index);

                if (function->type() != module->wasm_file()->functionTypes[arguments.typeIndex])
                    throw Trap("Invalid call indirect type");

                call_function(function);
                break;
            }

            case drop:
                (void)m_frame->stack.pop();
                break;
            case select_:
            case select_typed: {
                uint32_t value = m_frame->stack.pop_as<uint32_t>();

                Value val2 = m_frame->stack.pop();
                Value val1 = m_frame->stack.pop();

                m_frame->stack.push(value != 0 ? val1 : val2);
                break;
            }

            case local_get:
                m_frame->stack.push(m_frame->locals[instruction.get_arguments<uint32_t>()]);
                break;
            case local_set:
                m_frame->locals[instruction.get_arguments<uint32_t>()] = m_frame->stack.pop();
                break;
            case local_tee:
                m_frame->locals[instruction.get_arguments<uint32_t>()] = m_frame->stack.peek();
                break;
            case global_get:
                m_frame->stack.push(mod->get_global(instruction.get_arguments<uint32_t>())->get());
                break;
            case global_set:
                mod->get_global(instruction.get_arguments<uint32_t>())->set(m_frame->stack.pop());
                break;

            case table_get: {
                const auto table = mod->get_table(instruction.get_arguments<uint32_t>());

                const auto index = pop_address(table);

                m_frame->stack.push(table->get(index));
                break;
            }
            case table_set: {
                const auto table = mod->get_table(instruction.get_arguments<uint32_t>());

                const auto value = m_frame->stack.pop_as<Reference>();
                const auto index = pop_address(table);

                table->set(index, value);
                break;
            }

#define X(opcode, memoryType, targetType)                                                            \
    case opcode:                                                                                     \
        run_load_instruction<memoryType, targetType>(instruction.get_arguments<WasmFile::MemArg>()); \
        break;
                ENUMERATE_LOAD_OPERATIONS(X)
#undef X

#define X(opcode, memoryType, targetType)                                                             \
    case opcode:                                                                                      \
        run_store_instruction<memoryType, targetType>(instruction.get_arguments<WasmFile::MemArg>()); \
        break;
                ENUMERATE_STORE_OPERATIONS(X)
#undef X

            case memory_size: {
                const auto* memory = mod->get_memory(instruction.get_arguments<uint32_t>());
                m_frame->stack.push(to_address(memory->size(), memory));
                break;
            }
            case memory_grow: {
                auto* memory = mod->get_memory(instruction.get_arguments<uint32_t>());

                uint64_t addPages = pop_address(memory);

                auto max_pages = memory->address_type() == AddressType::i64 ? Validator::MAX_WASM_PAGES_I64 : Validator::MAX_WASM_PAGES_I32;
                if (memory->size() + addPages > (memory->max() ? *memory->max() : max_pages))
                {
                    m_frame->stack.push(to_address(-1, memory));
                    break;
                }

                m_frame->stack.push(to_address(memory->size(), memory));
                memory->grow(addPages);
                break;
            }

            case i32_const:
                m_frame->stack.push(instruction.get_arguments<uint32_t>());
                break;
            case i64_const:
                m_frame->stack.push(instruction.get_arguments<uint64_t>());
                break;
            case f32_const:
                m_frame->stack.push(instruction.get_arguments<float>());
                break;
            case f64_const:
                m_frame->stack.push(instruction.get_arguments<double>());
                break;

#define X(opcode, operation, type, resultType)              \
    case opcode:                                            \
        run_unary_operation<type, operation_##operation>(); \
        break;
                ENUMERATE_UNARY_OPERATIONS(X)
#undef X

#define X(opcode, operation, lhsType, rhsType, resultType)               \
    case opcode:                                                         \
        run_binary_operation<lhsType, rhsType, operation_##operation>(); \
        break;
                ENUMERATE_BINARY_OPERATIONS(X)
#undef X

            case ref_null:
                m_frame->stack.push(default_value_for_type(instruction.get_arguments<Type>()));
                break;
            case ref_is_null:
                m_frame->stack.push(static_cast<uint32_t>(!m_frame->stack.pop_as<Reference>().index));
                break;
            case ref_func:
                m_frame->stack.push(Reference { ReferenceType::Function, instruction.get_arguments<uint32_t>(), mod.get() });
                break;

            case memory_init: {
                const auto& arguments = instruction.get_arguments<MemoryInitArguments>();
                const auto* memory = mod->get_memory(arguments.memoryIndex);

                uint64_t count = pop_address(memory);
                uint64_t source = pop_address(memory);
                uint64_t destination = pop_address(memory);

                const auto& data = mod->wasm_file()->dataBlocks[arguments.dataIndex];

                if (static_cast<uint64_t>(source) + count > data.data.size())
                    throw Trap("Out of bounds memory init");

                if (memory->check_outside_bounds(destination, count))
                    throw Trap("Out of bounds memory init");

                memcpy(memory->data() + destination, data.data.data() + source, count);
                break;
            }
            case data_drop:
                mod->wasm_file()->dataBlocks[instruction.get_arguments<uint32_t>()] = WasmFile::Data();
                break;
            case memory_copy: {
                const auto& arguments = instruction.get_arguments<MemoryCopyArguments>();
                const auto* sourceMemory = mod->get_memory(arguments.source);
                const auto* destinationMemory = mod->get_memory(arguments.destination);

                const bool is_count64 = destinationMemory->address_type() == AddressType::i64 && sourceMemory->address_type() == AddressType::i64;
                uint64_t count = is_count64 ? m_frame->stack.pop_as<uint64_t>() : m_frame->stack.pop_as<uint32_t>();

                uint64_t source = pop_address(sourceMemory);
                uint64_t destination = pop_address(destinationMemory);

                if (sourceMemory->check_outside_bounds(source, count) || destinationMemory->check_outside_bounds(destination, count))
                    throw Trap("Out of bounds memory copy");

                if (count == 0)
                    break;

                if (destination <= source)
                    for (uint64_t i = 0; i < count; i++)
                        destinationMemory->data()[destination + i] = sourceMemory->data()[source + i];
                else
                    for (uint64_t i = count; i > 0; i--)
                        destinationMemory->data()[destination + i - 1] = sourceMemory->data()[source + i - 1];
                break;
            }
            case memory_fill: {
                const auto* memory = mod->get_memory(instruction.get_arguments<uint32_t>());

                uint64_t count = pop_address(memory);
                uint32_t value = m_frame->stack.pop_as<uint32_t>();
                uint64_t destination = pop_address(memory);

                if (memory->check_outside_bounds(destination, count))
                    throw Trap("Out of bounds memory fill");

                memset(memory->data() + destination, value, count);
                break;
            }

            case table_init: {
                const auto& arguments = instruction.get_arguments<TableInitArguments>();
                auto* table = mod->get_table(arguments.tableIndex);

                auto count = m_frame->stack.pop_as<uint32_t>();
                auto source = m_frame->stack.pop_as<uint32_t>();
                auto destination = pop_address(table);

                const auto& element = mod->wasm_file()->elements[arguments.elementIndex];
                size_t elemSize = element.functionIndexes.empty() ? element.referencesExpr.size() : element.functionIndexes.size();

                if (static_cast<uint64_t>(source) + count > elemSize || static_cast<uint64_t>(destination) + count > table->size())
                    throw Trap("Out of bounds table init");

                for (uint32_t i = 0; i < count; i++)
                {
                    if (element.functionIndexes.empty())
                    {
                        Value reference = run_bare_code(mod, element.referencesExpr[source + i]);
                        table->unsafe_set(destination + i, reference.get<Reference>());
                    }
                    else
                        table->unsafe_set(destination + i, Reference { ReferenceType::Function, element.functionIndexes[source + i], mod.get() });
                }
                break;
            }
            case elem_drop:
                mod->wasm_file()->elements[instruction.get_arguments<uint32_t>()] = WasmFile::Element();
                break;
            case table_copy: {
                const auto& arguments = instruction.get_arguments<TableCopyArguments>();
                const auto* sourceTable = mod->get_table(arguments.source);
                auto* destinationTable = mod->get_table(arguments.destination);

                const bool is_count64 = destinationTable->address_type() == AddressType::i64 && sourceTable->address_type() == AddressType::i64;
                auto count = is_count64 ? m_frame->stack.pop_as<uint64_t>() : m_frame->stack.pop_as<uint32_t>();

                auto source = pop_address(sourceTable);
                auto destination = pop_address(destinationTable);

                if (source + count > sourceTable->size() || destination + count > destinationTable->size())
                    throw Trap("Out of bounds table copy");

                if (count == 0)
                    break;

                if (destination <= source)
                {
                    for (uint32_t i = 0; i < count; i++)
                        destinationTable->unsafe_set(destination + i, sourceTable->unsafe_get(source + i));
                }
                else
                {
                    for (int64_t i = count - 1; i > -1; i--)
                        destinationTable->unsafe_set(destination + i, sourceTable->unsafe_get(source + i));
                }
                break;
            }
            case table_grow: {
                auto* table = mod->get_table(instruction.get_arguments<uint32_t>());

                auto addEntries = pop_address(table);
                auto value = m_frame->stack.pop_as<Reference>();

                if (table->size() + addEntries > (table->max() ? *table->max() : UINT32_MAX))
                {
                    m_frame->stack.push(to_address(-1, table));
                    break;
                }

                m_frame->stack.push(to_address(table->size(), table));
                table->grow(addEntries, value);

                break;
            }
            case table_size: {
                const auto* table = mod->get_table(instruction.get_arguments<uint32_t>());
                m_frame->stack.push(to_address(table->size(), table));
                break;
            }
            case table_fill: {
                auto* table = mod->get_table(instruction.get_arguments<uint32_t>());

                auto count = pop_address(table);
                auto value = m_frame->stack.pop_as<Reference>();
                auto destination = pop_address(table);

                if (destination + count > table->size())
                    throw Trap("Out of bounds table fill");

                for (uint32_t i = 0; i < count; i++)
                    table->unsafe_set(destination + i, value);

                break;
            }

            case Opcode::v128_load8_splat:
                run_load_vector_element_instruction<uint8x16_t, false>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load16_splat:
                run_load_vector_element_instruction<uint16x8_t, false>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load32_splat:
                run_load_vector_element_instruction<uint32x4_t, false>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load64_splat:
                run_load_vector_element_instruction<uint64x2_t, false>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load32_zero:
                run_load_vector_element_instruction<uint32x4_t, true>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case Opcode::v128_load64_zero:
                run_load_vector_element_instruction<uint64x2_t, true>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_const:
                m_frame->stack.push(instruction.get_arguments<uint128_t>());
                break;
            case i8x16_shuffle: {
                const auto& arg = instruction.get_arguments<uint8x16_t>();
                auto b = m_frame->stack.pop_as<uint8x16_t>();
                auto a = m_frame->stack.pop_as<uint8x16_t>();
                // TODO: Use __builtin_shuffle on GCC
                uint8x16_t result;
                for (size_t i = 0; i < 16; i++)
                {
                    if (arg[i] < 16)
                        result[i] = a[arg[i]];
                    else
                        result[i] = b[arg[i] - 16];
                }
                m_frame->stack.push(result);
                break;
            }
            case i8x16_extract_lane_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<int8x16_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i8x16_extract_lane_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<uint8x16_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i8x16_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint8x16_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case i16x8_extract_lane_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<int16x8_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i16x8_extract_lane_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<uint16x8_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i16x8_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint16x8_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case i32x4_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<uint32x4_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i32x4_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint32x4_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case i64x2_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<uint64x2_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case i64x2_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint64_t>();
                auto vector = m_frame->stack.pop_as<uint64x2_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case f32x4_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<float32x4_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case f32x4_replace_lane: {
                auto lane = m_frame->stack.pop_as<float>();
                auto vector = m_frame->stack.pop_as<float32x4_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case f64x2_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<float64x2_t>()[instruction.get_arguments<uint8_t>()]);
                break;
            case f64x2_replace_lane: {
                auto lane = m_frame->stack.pop_as<double>();
                auto vector = m_frame->stack.pop_as<float64x2_t>();
                vector[instruction.get_arguments<uint8_t>()] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case v128_bitselect: {
                uint128_t mask = m_frame->stack.pop_as<uint128_t>();
                uint128_t falseVector = m_frame->stack.pop_as<uint128_t>();
                uint128_t trueVector = m_frame->stack.pop_as<uint128_t>();
                m_frame->stack.push((trueVector & mask) | (falseVector & ~mask));
                break;
            }
            case v128_load8_lane:
                run_load_lane_instruction<uint8x16_t, uint8_t, uint8_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_load16_lane:
                run_load_lane_instruction<uint16x8_t, uint16_t, uint16_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_load32_lane:
                run_load_lane_instruction<uint32x4_t, uint32_t, uint32_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_load64_lane:
                run_load_lane_instruction<uint64x2_t, uint64_t, uint64_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_store8_lane:
                run_store_lane_instruction<uint8x16_t, uint8_t, uint8_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_store16_lane:
                run_store_lane_instruction<uint16x8_t, uint16_t, uint16_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_store32_lane:
                run_store_lane_instruction<uint32x4_t, uint32_t, uint32_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            case v128_store64_lane:
                run_store_lane_instruction<uint64x2_t, uint64_t, uint64_t>(instruction.get_arguments<LoadStoreLaneArguments>());
                break;
            default:
                throw Trap(std::format("Unknown opcode {:#x}", static_cast<uint32_t>(instruction.opcode)));
        }
    }

#ifdef DEBUG_BUILD
    if (m_frame->stack.size() != type.returns.size())
        throw Trap("Extra elements on stack at the end of a function");
#endif

    return std::move(m_frame->stack.pop_n_values(type.returns.size()));
}

Ref<Module> VM::get_registered_module(const std::string& name)
{
    return m_registered_modules[name];
}

Value VM::run_bare_code(Ref<RealModule> mod, std::span<const Instruction> instructions)
{
    ValueStack stack;

    const auto run_binary_operation = [&stack]<IsValueType T, typename Op>(Op op) {
        const auto rhs = stack.pop_as<T>();
        const auto lhs = stack.pop_as<T>();
        stack.push(op(lhs, rhs));
    };

    for (const auto& instruction : instructions)
    {
        switch (instruction.opcode)
        {
            using enum Opcode;
            case end:
                break;
            case global_get:
                stack.push(mod->get_global(instruction.get_arguments<uint32_t>())->get());
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
            case i32_add:
                run_binary_operation.operator()<uint32_t>([](auto lhs, auto rhs) { return lhs + rhs; });
                break;
            case i32_sub:
                run_binary_operation.operator()<uint32_t>([](auto lhs, auto rhs) { return lhs - rhs; });
                break;
            case i32_mul:
                run_binary_operation.operator()<uint32_t>([](auto lhs, auto rhs) { return lhs * rhs; });
                break;
            case i64_add:
                run_binary_operation.operator()<uint64_t>([](auto lhs, auto rhs) { return lhs + rhs; });
                break;
            case i64_sub:
                run_binary_operation.operator()<uint64_t>([](auto lhs, auto rhs) { return lhs - rhs; });
                break;
            case i64_mul:
                run_binary_operation.operator()<uint64_t>([](auto lhs, auto rhs) { return lhs * rhs; });
                break;
            case ref_null:
                stack.push(default_value_for_type(instruction.get_arguments<Type>()));
                break;
            case ref_func:
                stack.push(Reference { ReferenceType::Function, instruction.get_arguments<uint32_t>(), mod.get() });
                break;
            case v128_const:
                stack.push(instruction.get_arguments<uint128_t>());
                break;
            default:
                throw Trap(std::format("Unknown or disallowed in bare code opcode {:#x}", static_cast<uint32_t>(instruction.opcode)));
        }
    }

#ifdef DEBUG_BUILD
    if (stack.size() != 1)
        throw Trap("Extra elements on stack at the end of bare code");
#endif

    return stack.pop();
}

Memory* VM::get_current_frame_memory_0()
{
    // FIXME: Verify there is a memory 0
    return m_frame->mod->get_memory(0);
}

void VM::clean_up_frame()
{
    delete m_frame;
    m_frame = m_frame_stack.pop();
}

template <typename LhsType, typename RhsType, Value(function)(LhsType, RhsType)>
RELEASE_INLINE void VM::run_binary_operation()
{
    RhsType rhs = m_frame->stack.pop_as<RhsType>();
    LhsType lhs = m_frame->stack.pop_as<LhsType>();
    m_frame->stack.push(function(lhs, rhs));
}

template <typename T, Value(function)(T)>
RELEASE_INLINE void VM::run_unary_operation()
{
    T a = m_frame->stack.pop_as<T>();
    m_frame->stack.push(function(a));
}

template <typename ActualType, IsValueType StackType>
RELEASE_INLINE void VM::run_load_instruction(const WasmFile::MemArg& memArg)
{
    const auto* memory = m_frame->mod->get_memory(memArg.memory_index);

    uint64_t address = pop_address(memory);

    if (memory->check_outside_bounds(address, memArg.offset + sizeof(ActualType)))
        throw Trap("Out of bounds load");

    ActualType value;
    memcpy(&value, &memory->data()[address + memArg.offset], sizeof(ActualType));

    if constexpr (IsVector<ActualType>)
        m_frame->stack.push(std::bit_cast<ToValueType<StackType>>(__builtin_convertvector(value, StackType)));
    else
        m_frame->stack.push(static_cast<StackType>(value));
}

template <typename ActualType, IsValueType StackType>
RELEASE_INLINE void VM::run_store_instruction(const WasmFile::MemArg& memArg)
{
    const auto* memory = m_frame->mod->get_memory(memArg.memory_index);

    ActualType value = static_cast<ActualType>(m_frame->stack.pop_as<StackType>());
    uint64_t address = pop_address(memory);

    if (memory->check_outside_bounds(address, memArg.offset + sizeof(ActualType)))
        throw Trap("Out of bounds store");

    memcpy(&memory->data()[address + memArg.offset], &value, sizeof(ActualType));
}

void VM::branch_to_label(Label label)
{
    m_frame->stack.erase(label.stackHeight, label.arity);
    m_frame->ip = label.continuation;
}

void VM::call_function(Ref<Function> function)
{
    const auto args = m_frame->stack.pop_n_values(function->type().params.size());
    const auto returnedValues = function->run(args);
    m_frame->stack.push_values(returnedValues);
}

template <IsVector VectorType, bool Zero>
void VM::run_load_vector_element_instruction(const WasmFile::MemArg& memArg)
{
    const auto* memory = m_frame->mod->get_memory(memArg.memory_index);

    auto address = pop_address(memory);

    if (memory->check_outside_bounds(address, memArg.offset + sizeof(VectorElement<VectorType>)))
        throw Trap("Out of bounds load");

    VectorElement<VectorType> value {};
    memcpy(&value, &memory->data()[address + memArg.offset], sizeof(VectorElement<VectorType>));

    if constexpr (Zero)
    {
        VectorType vector {};
        vector[0] = value;
        m_frame->stack.push(vector);
    }
    else
        m_frame->stack.push(vector_broadcast<VectorType>(std::move(value)));
}

template <IsVector VectorType, typename ActualType, typename LaneType>
void VM::run_load_lane_instruction(const LoadStoreLaneArguments& args)
{
    const auto* memory = m_frame->mod->get_memory(args.memArg.memory_index);

    auto vector = m_frame->stack.pop_as<VectorType>();
    auto address = pop_address(memory);

    if (memory->check_outside_bounds(address, args.memArg.offset + sizeof(ActualType)))
        throw Trap("Out of bounds load");

    ActualType value;
    memcpy(&value, &memory->data()[address + args.memArg.offset], sizeof(ActualType));

    vector[args.lane] = (LaneType)value;
    m_frame->stack.push(vector);
}

template <IsVector VectorType, typename ActualType, typename StackType>
void VM::run_store_lane_instruction(const LoadStoreLaneArguments& args)
{
    const auto* memory = m_frame->mod->get_memory(args.memArg.memory_index);

    auto vector = m_frame->stack.pop_as<VectorType>();
    auto address = pop_address(memory);

    if (memory->check_outside_bounds(address, args.memArg.offset + sizeof(ActualType)))
        throw Trap("Out of bounds store");

    ActualType value = vector[args.lane];
    memcpy(&memory->data()[address + args.memArg.offset], &value, sizeof(ActualType));
}

template <HasAddressType Structure>
uint64_t VM::pop_address(const Structure* structure)
{
    switch (structure->address_type())
    {
        using enum AddressType;
        case i32:
            return m_frame->stack.pop_as<uint32_t>();
        case i64:
            return m_frame->stack.pop_as<uint64_t>();
        default:
            std::unreachable();
    }
}

template <HasAddressType Structure>
Value VM::to_address(uint64_t value, const Structure* structure)
{
    switch (structure->address_type())
    {
        using enum AddressType;
        case i32:
            // FIXME: maybe add a debug check if the cast doesnt lose data
            return static_cast<uint32_t>(value);
        case i64:
            return value;
        default:
            std::unreachable();
    }
}

VM::ImportLocation VM::find_import(std::string_view environment, std::string_view name, WasmFile::ImportType type)
{
    for (const auto& [module_name, module] : m_registered_modules)
    {
        if (module_name == environment)
        {
            auto maybeImported = module->try_import(name, type);
            if (!maybeImported.has_value())
                throw Trap(std::format("Unknown or invalid import: {}:{}", environment, name));
            return ImportLocation { module, maybeImported.value() };
        }
    }

    throw Trap(std::format("Unknown or invalid import: {}:{}", environment, name));
}
