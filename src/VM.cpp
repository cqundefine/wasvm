#include <Opcode.h>
#include <Operators.h>
#include <SIMD.h>
#include <Type.h>
#include <Util.h>
#include <VM.h>
#include <Value.h>
#include <WasmFile.h>
#include <cstring>
#include <limits>
#include <print>

Ref<Module> VM::load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current)
{
    auto new_module = MakeRef<Module>(m_next_module_id++);
    new_module->wasmFile = file;

    for (const auto& import : new_module->wasmFile->imports)
    {
        ImportLocation location = find_import(import.environment, import.name, import.type);
        switch (import.type)
        {
            case WasmFile::ImportType::Function: {
                const auto function = location.module->functions[location.index];
                if (function->type != file->functionTypes[import.functionTypeIndex])
                    throw Trap();
                new_module->functions.push_back(function);
                break;
            }
            case WasmFile::ImportType::Table: {
                const auto table = location.module->get_table(location.index);
                if (table->type != import.tableRefType || !WasmFile::Limits(table->elements.size(), table->max).fits_within(import.tableLimits))
                    throw Trap();
                new_module->add_table(table);
                break;
            }
            case WasmFile::ImportType::Memory: {
                const auto memory = location.module->get_memory(location.index);
                if (!WasmFile::Limits(memory->size, memory->max).fits_within(import.memoryLimits))
                    throw Trap();
                new_module->add_memory(memory);
                break;
            }
            case WasmFile::ImportType::Global: {
                const auto global = location.module->get_global(location.index);
                if (global->type != import.globalType || global->mut != import.globalMut)
                    throw Trap();
                new_module->add_global(global);
                break;
            }
            default:
                assert(false);
        }
    }

    for (size_t i = 0; i < new_module->wasmFile->functionTypeIndexes.size(); i++)
    {
        auto function = MakeRef<Function>();
        function->type = new_module->wasmFile->functionTypes[new_module->wasmFile->functionTypeIndexes[i]];
        function->mod = new_module;
        function->code = new_module->wasmFile->codeBlocks[i];
        new_module->functions.push_back(function);
    }

    for (const auto& global : new_module->wasmFile->globals)
        new_module->add_global(MakeRef<Global>(global.type, global.mut, run_bare_code(new_module, global.initCode)));

    for (const auto& memory : new_module->wasmFile->memories)
        new_module->add_memory(MakeRef<Memory>(memory));

    for (const auto& tableInfo : new_module->wasmFile->tables)
    {
        auto table = MakeRef<Table>(tableInfo.refType);
        table->max = tableInfo.limits.max;
        table->elements.reserve(tableInfo.limits.min);
        for (uint32_t i = 0; i < tableInfo.limits.min; i++)
            table->elements.push_back(Reference { get_reference_type_from_reftype(tableInfo.refType), UINT32_MAX, new_module.get() });
        new_module->add_table(table);
    }

    for (auto& element : new_module->wasmFile->elements)
    {
        if (element.mode == WasmFile::ElementMode::Active)
        {
            Value beginValue = run_bare_code(new_module, element.expr);
            assert(beginValue.holds_alternative<uint32_t>());
            uint32_t begin = beginValue.get<uint32_t>();

            size_t size = element.functionIndexes.empty() ? element.referencesExpr.size() : element.functionIndexes.size();

            if (begin + size > new_module->get_table(element.table)->elements.size())
                throw Trap();

            for (size_t i = 0; i < size; i++)
            {
                if (element.functionIndexes.empty())
                {
                    Value reference = run_bare_code(new_module, element.referencesExpr[i]);
                    assert(reference.holds_alternative<Reference>());
                    new_module->get_table(element.table)->elements[begin + i] = reference.get<Reference>();
                }
                else
                {
                    new_module->get_table(element.table)->elements[begin + i] = Reference { ReferenceType::Function, element.functionIndexes[i], new_module.get() };
                }
            }
        }

        if (element.mode == WasmFile::ElementMode::Active || element.mode == WasmFile::ElementMode::Declarative)
        {
            element.table = UINT32_MAX;
            element.expr.clear();
            element.functionIndexes.clear();
            element.referencesExpr.clear();
        }
    }

    for (auto& data : new_module->wasmFile->dataBlocks)
    {
        if (data.mode == WasmFile::ElementMode::Active)
        {
            Value beginValue = run_bare_code(new_module, data.expr);
            assert(beginValue.holds_alternative<uint32_t>());
            uint32_t begin = beginValue.get<uint32_t>();

            auto memory = new_module->get_memory(data.memoryIndex);

            if (begin + data.data.size() > memory->size * WASM_PAGE_SIZE)
                throw Trap();

            memcpy(memory->data + begin, data.data.data(), data.data.size());

            data.type = UINT32_MAX;
            data.expr.clear();
            data.data.clear();
        }
    }

    if (new_module->wasmFile->startFunction)
        run_function(new_module, *new_module->wasmFile->startFunction, {});

    if (!dont_make_current)
        m_current_module = new_module;

    return new_module;
}

void VM::register_module(const std::string& name, Ref<Module> module)
{
    m_registered_modules[name] = module;
}

std::vector<Value> VM::run_function(const std::string& name, const std::vector<Value>& args)
{
    return run_function(m_current_module, name, args);
}

std::vector<Value> VM::run_function(const std::string& mod, const std::string& name, const std::vector<Value>& args)
{
    return run_function(m_registered_modules[mod], name, args);
}

std::vector<Value> VM::run_function(Ref<Module> mod, const std::string& name, const std::vector<Value>& args)
{
    WasmFile::Export functionExport = mod->wasmFile->find_export_by_name(name);
    assert(functionExport.type == WasmFile::ImportType::Function);
    return run_function(mod, functionExport.index, args);
}

std::vector<Value> VM::run_function(Ref<Module> mod, uint32_t index, const std::vector<Value>& args)
{
    return run_function(mod, mod->functions[index], args);
}

std::vector<Value> VM::run_function(Ref<Module> mod, Ref<Function> function, const std::vector<Value>& args)
{
    if (m_frame_stack.size() >= MAX_FRAME_STACK_SIZE)
        throw Trap();

    m_frame_stack.push(m_frame);
    m_frame = new Frame(mod);

    DEFER(clean_up_frame());

    if (args.size() != function->type.params.size())
        throw Trap();

    for (const auto& param : args)
        m_frame->locals.push_back(param);

    for (const auto local : function->code.locals)
        m_frame->locals.push_back(default_value_for_type(local));

    while (m_frame->ip < function->code.instructions.size())
    {
        const Instruction& instruction = function->code.instructions[m_frame->ip++];

        switch (instruction.opcode)
        {
            using enum Opcode;
            case unreachable:
                throw Trap();
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
                return std::move(m_frame->stack.pop_n_values(function->type.returns.size()));
            case call:
                call_function(mod->functions[instruction.get_arguments<uint32_t>()]);
                break;
            case call_indirect: {
                const auto& arguments = instruction.get_arguments<CallIndirectArguments>();

                uint32_t index = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(arguments.tableIndex);

                if (index >= table->elements.size())
                    throw Trap();

                Reference reference = table->elements[index];

                if (reference.index == UINT32_MAX)
                    throw Trap();

                if (reference.type != ReferenceType::Function)
                    throw Trap();

                auto* module = reference.module ? reference.module : mod.get();
                auto function = module->functions[reference.index];

                if (function->type != module->wasmFile->functionTypes[arguments.typeIndex])
                    throw Trap();

                call_function(function);
                break;
            }
            case drop:
                m_frame->stack.pop();
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
                m_frame->stack.push(mod->get_global(instruction.get_arguments<uint32_t>())->value);
                break;
            case global_set:
                mod->get_global(instruction.get_arguments<uint32_t>())->value = m_frame->stack.pop();
                break;
            case table_get: {
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                auto table = mod->get_table(instruction.get_arguments<uint32_t>());
                if (index >= table->elements.size())
                    throw Trap();
                m_frame->stack.push(table->elements[index]);
                break;
            }
            case table_set: {
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                auto table = mod->get_table(instruction.get_arguments<uint32_t>());
                if (index >= table->elements.size())
                    throw Trap();
                table->elements[index] = value;
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

            case memory_size:
                m_frame->stack.push(mod->get_memory(instruction.get_arguments<uint32_t>())->size);
                break;
            case memory_grow: {
                auto memory = mod->get_memory(instruction.get_arguments<uint32_t>());

                uint32_t addPages = m_frame->stack.pop_as<uint32_t>();

                if (memory->size + addPages > (memory->max ? *memory->max : WasmFile::MAX_WASM_PAGES))
                {
                    m_frame->stack.push((uint32_t)-1);
                    break;
                }

                m_frame->stack.push(memory->size);
                uint32_t newSize = (memory->size + addPages) * WASM_PAGE_SIZE;
                uint8_t* newMemory = (uint8_t*)malloc(newSize);
                memset(newMemory, 0, newSize);
                memcpy(newMemory, memory->data, std::min(memory->size * WASM_PAGE_SIZE, newSize));
                free(memory->data);
                memory->data = newMemory;
                memory->size += addPages;
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
                m_frame->stack.push((uint32_t)(m_frame->stack.pop_as<Reference>().index == UINT32_MAX));
                break;
            case ref_func:
                m_frame->stack.push(Reference { ReferenceType::Function, instruction.get_arguments<uint32_t>(), mod.get() });
                break;
            case memory_init: {
                const auto& arguments = instruction.get_arguments<MemoryInitArguments>();
                auto memory = mod->get_memory(arguments.memoryIndex);

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                const WasmFile::Data& data = mod->wasmFile->dataBlocks[arguments.dataIndex];

                if ((uint64_t)source + count > data.data.size())
                    throw Trap();

                if ((uint64_t)destination + count > memory->size * WASM_PAGE_SIZE)
                    throw Trap();

                memcpy(memory->data + destination, data.data.data() + source, count);
                break;
            }
            case data_drop: {
                WasmFile::Data& data = mod->wasmFile->dataBlocks[instruction.get_arguments<uint32_t>()];
                data.type = UINT32_MAX;
                data.expr.clear();
                data.data.clear();
                break;
            }
            case memory_copy: {
                const auto& arguments = instruction.get_arguments<MemoryCopyArguments>();
                auto sourceMemory = mod->get_memory(arguments.source);
                auto destinationMemory = mod->get_memory(arguments.destination);

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)source + count > sourceMemory->size * WASM_PAGE_SIZE || (uint64_t)destination + count > destinationMemory->size * WASM_PAGE_SIZE)
                    throw Trap();

                memcpy(sourceMemory->data + destination, destinationMemory->data + source, count);
                break;
            }
            case memory_fill: {
                auto memory = mod->get_memory(instruction.get_arguments<uint32_t>());

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t val = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)destination + count > memory->size * WASM_PAGE_SIZE)
                    throw Trap();

                memset(memory->data + destination, val, count);
                break;
            }
            case table_init: {
                const auto& arguments = instruction.get_arguments<TableInitArguments>();
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(arguments.tableIndex);

                const auto& elem = mod->wasmFile->elements[arguments.elementIndex];
                size_t elemSize = elem.functionIndexes.empty() ? elem.referencesExpr.size() : elem.functionIndexes.size();

                if ((uint64_t)source + count > elemSize || (uint64_t)destination + count > table->elements.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                {
                    if (elem.functionIndexes.empty())
                    {
                        Value reference = run_bare_code(mod, elem.referencesExpr[source + i]);
                        assert(reference.holds_alternative<Reference>());
                        table->elements[destination + i] = reference.get<Reference>();
                    }
                    else
                        table->elements[destination + i] = Reference { ReferenceType::Function, elem.functionIndexes[source + i], mod.get() };
                }
                break;
            }
            case elem_drop: {
                WasmFile::Element& elem = mod->wasmFile->elements[instruction.get_arguments<uint32_t>()];
                elem.table = UINT32_MAX;
                elem.expr.clear();
                elem.functionIndexes.clear();
                elem.referencesExpr.clear();
                break;
            }
            case table_copy: {
                const auto& arguments = instruction.get_arguments<TableCopyArguments>();
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t source = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto destinationTable = mod->get_table(arguments.destination);
                auto sourceTable = mod->get_table(arguments.source);

                if ((uint64_t)source + count > sourceTable->elements.size() || (uint64_t)destination + count > destinationTable->elements.size())
                    throw Trap();

                if (count == 0)
                    break;

                if (destination <= source)
                {
                    for (uint32_t i = 0; i < count; i++)
                        destinationTable->elements[destination + i] = sourceTable->elements[source + i];
                }
                else
                {
                    for (int64_t i = count - 1; i > -1; i--)
                        destinationTable->elements[destination + i] = sourceTable->elements[source + i];
                }
                break;
            }
            case table_grow: {
                uint32_t addEntries = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(instruction.get_arguments<uint32_t>());
                uint32_t oldSize = table->elements.size();

                Reference value = m_frame->stack.pop_as<Reference>();

                if ((uint64_t)table->elements.size() + addEntries > (table->max ? *table->max : UINT32_MAX))
                {
                    m_frame->stack.push((uint32_t)-1);
                    break;
                }

                m_frame->stack.push(oldSize);

                assert(addEntries >= 0);
                for (uint32_t i = 0; i < addEntries; i++)
                    table->elements.push_back(value);

                break;
            }
            case table_size:
                m_frame->stack.push((uint32_t)mod->get_table(instruction.get_arguments<uint32_t>())->elements.size());
                break;
            case table_fill: {
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(instruction.get_arguments<uint32_t>());

                if (destination + count > table->elements.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                    table->elements[destination + i] = value;

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
                std::println(std::cerr, "Error: Unknown opcode {:#x}", static_cast<uint32_t>(instruction.opcode));
                throw Trap();
        }
    }

    std::vector<Value> returnValues;
    if (function->type.returns.size() > 0)
    {
        for (size_t i = function->type.returns.size() - 1; i != (size_t)-1; i--)
        {
            auto returnValue = m_frame->stack.pop();
#ifdef DEBUG_BUILD
            if (get_value_type(returnValue) != function->type.returns[i])
            {
                std::println(std::cerr, "Error: Unxpected return value on the stack: {}, expected {}", get_type_name(get_value_type(returnValue)), get_type_name(function->type.returns[i]));
                throw Trap();
            }
#endif
            returnValues.push_back(returnValue);
        }

        std::reverse(returnValues.begin(), returnValues.end());
    }

#ifdef DEBUG_BUILD
    if (m_frame->stack.size() != 0)
    {
        std::println(std::cerr, "Error: Stack not empty when running function, size is {}", m_frame->stack.size());
        throw Trap();
    }
#endif

    return std::move(returnValues);
}

Ref<Module> VM::get_registered_module(const std::string& name)
{
    return m_registered_modules[name];
}

Value VM::run_bare_code(Ref<Module> mod, const std::vector<Instruction>& instructions)
{
    ValueStack stack;

    for (const auto& instruction : instructions)
    {
        switch (instruction.opcode)
        {
            using enum Opcode;
            case end:
                break;
            case global_get:
                stack.push(mod->get_global(instruction.get_arguments<uint32_t>())->value);
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
            // NOTE: We don't check if extended const is enabled, because it would've failed at the validator stage
            case i32_add:
                stack.push(stack.pop_as<uint32_t>() + stack.pop_as<uint32_t>());
                break;
            case i32_sub:
                stack.push(stack.pop_as<uint32_t>() - stack.pop_as<uint32_t>());
                break;
            case i32_mul:
                stack.push(stack.pop_as<uint32_t>() * stack.pop_as<uint32_t>());
                break;
            case i64_add:
                stack.push(stack.pop_as<uint64_t>() + stack.pop_as<uint64_t>());
                break;
            case i64_sub:
                stack.push(stack.pop_as<uint64_t>() - stack.pop_as<uint64_t>());
                break;
            case i64_mul:
                stack.push(stack.pop_as<uint64_t>() * stack.pop_as<uint64_t>());
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
                std::println(std::cerr, "Error: Unknown or disallowed in bare code opcode {:#x}", static_cast<uint32_t>(instruction.opcode));
                throw Trap();
        }
    }

#ifdef DEBUG_BUILD
    if (stack.size() != 1)
        throw Trap();
#endif

    return stack.pop();
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
void VM::run_load_instruction(const WasmFile::MemArg& memArg)
{
    auto memory = m_frame->mod->get_memory(memArg.memoryIndex);

    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    ActualType value;
    memcpy(&value, &memory->data[address + memArg.offset], sizeof(ActualType));

    if constexpr (IsVector<ActualType>)
        m_frame->stack.push(std::bit_cast<ToValueType<StackType>>(__builtin_convertvector(value, StackType)));
    else
        m_frame->stack.push(static_cast<StackType>(value));
}

template <typename ActualType, typename StackType>
void VM::run_store_instruction(const WasmFile::MemArg& memArg)
{
    auto memory = m_frame->mod->get_memory(memArg.memoryIndex);

    ActualType value = (ActualType)m_frame->stack.pop_as<StackType>();
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(ActualType) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    memcpy(&memory->data[address + memArg.offset], &value, sizeof(ActualType));
}

void VM::branch_to_label(Label label)
{
    m_frame->stack.erase(label.stackHeight, label.arity);
    m_frame->ip = label.continuation;
}

void VM::call_function(Ref<Function> function)
{
    std::vector<Value> args = m_frame->stack.pop_n_values(function->type.params.size());

    // FIXME: Are we sure the return values are correct
    std::vector<Value> returnedValues = run_function(function->mod, function, args);
    for (const auto& returned : returnedValues)
        m_frame->stack.push(returned);
}

template <IsVector VectorType, bool Zero>
void VM::run_load_vector_element_instruction(const WasmFile::MemArg& memArg)
{
    auto memory = m_frame->mod->get_memory(memArg.memoryIndex);

    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + sizeof(VectorElement<VectorType>) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    VectorElement<VectorType> value {};
    memcpy(&value, &memory->data[address + memArg.offset], sizeof(VectorElement<VectorType>));

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
    auto memory = m_frame->mod->get_memory(args.memArg.memoryIndex);

    VectorType vector = m_frame->stack.pop_as<VectorType>();
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + args.memArg.offset + sizeof(ActualType) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    ActualType value;
    memcpy(&value, &memory->data[address + args.memArg.offset], sizeof(ActualType));

    vector[args.lane] = (LaneType)value;
    m_frame->stack.push(vector);
}

template <IsVector VectorType, typename ActualType, typename StackType>
void VM::run_store_lane_instruction(const LoadStoreLaneArguments& args)
{
    auto memory = m_frame->mod->get_memory(args.memArg.memoryIndex);

    VectorType vector = m_frame->stack.pop_as<VectorType>();
    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + args.memArg.offset + sizeof(ActualType) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    ActualType value = vector[args.lane];
    memcpy(&memory->data[address + args.memArg.offset], &value, sizeof(ActualType));
}

VM::ImportLocation VM::find_import(const std::string& environment, const std::string& name, WasmFile::ImportType importType)
{
    for (const auto& [module_name, module] : m_registered_modules)
    {
        if (module_name == environment)
        {
            WasmFile::Export exp = module->wasmFile->find_export_by_name(name);
            if (exp.type != importType)
                throw Trap();
            return ImportLocation { module, exp.index };
        }
    }

    throw Trap();
}
