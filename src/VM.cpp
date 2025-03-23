#include "WasmFile.h"
#include <Compiler.h>
#include <Opcode.h>
#include <Operators.h>
#include <Type.h>
#include <Util.h>
#include <VM.h>
#include <cstring>
#include <print>

Ref<Module> VM::load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current)
{
    auto new_module = MakeRef<Module>();
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
                // if (table->type != import.tableRefType || import.tableLimits <= WasmFile::Limits(table->elements.size(), table->max))
                //     throw Trap();
                new_module->add_table(table);
                break;
            }
            case WasmFile::ImportType::Memory: {
                const auto memory = location.module->get_memory(location.index);
                // if (import.memoryLimits <= WasmFile::Limits(memory->size, memory->max))
                //     throw Trap();
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

    for (const auto& global : new_module->wasmFile->globals)
        new_module->add_global(MakeRef<Global>(global.type, global.mut, run_bare_code_returning(new_module, global.initCode, global.type)));

    for (const auto& memory : new_module->wasmFile->memories)
        new_module->add_memory(MakeRef<Memory>(memory));

    for (auto& data : new_module->wasmFile->dataBlocks)
    {
        if (data.mode == WasmFile::ElementMode::Active)
        {
            Value beginValue = run_bare_code_returning(new_module, data.expr, Type::i32);
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

    for (const auto& tableInfo : new_module->wasmFile->tables)
    {
        auto table = MakeRef<Table>(tableInfo.refType);
        table->max = tableInfo.limits.max;
        table->elements.reserve(tableInfo.limits.min);
        for (uint32_t i = 0; i < tableInfo.limits.min; i++)
            table->elements.push_back(Reference { get_reference_type_from_reftype(tableInfo.refType), UINT32_MAX });
        new_module->add_table(table);
    }

    for (auto& element : new_module->wasmFile->elements)
    {
        if (element.mode == WasmFile::ElementMode::Active)
        {
            Value beginValue = run_bare_code_returning(new_module, element.expr, Type::i32);
            assert(beginValue.holds_alternative<uint32_t>());
            uint32_t begin = beginValue.get<uint32_t>();

            if (begin + (element.functionIndexes.empty() ? element.referencesExpr.size() : element.functionIndexes.size()) > new_module->get_table(element.table)->elements.size())
                throw Trap();

            for (size_t i = 0; i < element.functionIndexes.size(); i++)
            {
                if (element.functionIndexes.empty())
                {
                    Value reference = run_bare_code_returning(new_module, element.referencesExpr[i], Type::funcref);
                    assert(reference.holds_alternative<Reference>());
                    new_module->get_table(element.table)->elements[begin + i] = reference.get<Reference>();
                }
                else
                    new_module->get_table(element.table)->elements[begin + i] = Reference { ReferenceType::Function, element.functionIndexes[i] };
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

    for (size_t i = 0; i < new_module->wasmFile->functionTypeIndexes.size(); i++)
    {
        auto function = MakeRef<Function>();
        function->type = new_module->wasmFile->functionTypes[new_module->wasmFile->functionTypeIndexes[i]];
        function->mod = new_module;
        function->code = new_module->wasmFile->codeBlocks[i];
        new_module->functions.push_back(function);
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
    m_frame_stack.push(m_frame);
    m_frame = new Frame(mod);

    // FIXME: Are we sure that args is the correct size
    for (const auto& param : args)
        m_frame->locals.push_back(param);

    for (const auto local : function->code.locals)
        m_frame->locals.push_back(default_value_for_type(local));

    if (m_force_jit)
    {
        try
        {
            auto jittedCode = Compiler::compile(function, mod->wasmFile);
            Value returnValue;
            jittedCode(m_frame->locals.data(), &returnValue);
            clean_up_frame();
            if (function->type.returns.size() == 0)
                return {};
            else
                return { returnValue };
        }
        catch (JITCompilationException error)
        {
            std::println(std::cerr, "Failed to compile JIT");
            throw JITCompilationException();
        }
    }

    m_frame->label_stack.push_back(Label {
        .continuation = static_cast<uint32_t>(function->code.instructions.size()),
        .arity = static_cast<uint32_t>(function->type.returns.size()),
        .stackHeight = 0 });

    while (m_frame->ip < function->code.instructions.size())
    {
        const Instruction& instruction = function->code.instructions[m_frame->ip++];

        switch (instruction.opcode)
        {
            using enum Opcode;
            case unreachable:
                throw Trap();
            case nop:
                break;
            case block:
            case loop: {
                const auto& arguments = instruction.get_arguments<BlockLoopArguments>();
                Label label = arguments.label;
                label.stackHeight = static_cast<uint32_t>(m_frame->stack.size() - arguments.blockType.get_param_types(mod->wasmFile).size());
                m_frame->label_stack.push_back(label);
                break;
            }
            case if_: {
                const auto& arguments = instruction.get_arguments<IfArguments>();

                uint32_t value = m_frame->stack.pop_as<uint32_t>();

                Label label = arguments.endLabel;
                label.stackHeight = static_cast<uint32_t>(m_frame->stack.size() - arguments.blockType.get_param_types(mod->wasmFile).size());

                if (arguments.elseLocation.has_value())
                {
                    if (value == 0)
                        m_frame->ip = arguments.elseLocation.value() + 1;
                    m_frame->label_stack.push_back(label);
                }
                else
                {
                    if (value != 0)
                        m_frame->label_stack.push_back(label);
                    else
                        m_frame->ip = label.continuation;
                }
                break;
            }
            case else_:
                m_frame->ip = instruction.get_arguments<Label>().continuation;
                [[fallthrough]];
            case end:
                m_frame->label_stack.pop_back();
                break;
            case br:
                branch_to_label(instruction.get_arguments<uint32_t>());
                break;
            case br_if:
                if (m_frame->stack.pop_as<uint32_t>() != 0)
                    branch_to_label(instruction.get_arguments<uint32_t>());
                break;
            case br_table: {
                const auto& arguments = instruction.get_arguments<BranchTableArguments>();
                uint32_t i = m_frame->stack.pop_as<uint32_t>();
                if (i < arguments.labels.size())
                    branch_to_label(arguments.labels[i]);
                else
                    branch_to_label(arguments.defaultLabel);
                break;
            }
            case return_: {
                std::vector<Value> returnValues = m_frame->stack.pop_n_values(function->type.returns.size());
                clean_up_frame();
                return returnValues;
            }
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

                auto function = mod->functions[reference.index];

                if (function->type != mod->wasmFile->functionTypes[arguments.typeIndex])
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
            case i32_load:
                run_load_instruction<uint32_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load:
                run_load_instruction<uint64_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case f32_load:
                run_load_instruction<float, float>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case f64_load:
                run_load_instruction<double, double>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_load8_s:
                run_load_instruction<int8_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_load8_u:
                run_load_instruction<uint8_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_load16_s:
                run_load_instruction<int16_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_load16_u:
                run_load_instruction<uint16_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load8_s:
                run_load_instruction<int8_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load8_u:
                run_load_instruction<uint8_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load16_s:
                run_load_instruction<int16_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load16_u:
                run_load_instruction<uint16_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load32_s:
                run_load_instruction<int32_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_load32_u:
                run_load_instruction<uint32_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_store:
                run_store_instruction<uint32_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_store:
                run_store_instruction<uint64_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case f32_store:
                run_store_instruction<float, float>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case f64_store:
                run_store_instruction<double, double>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_store8:
                run_store_instruction<uint8_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i32_store16:
                run_store_instruction<uint16_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_store8:
                run_store_instruction<uint8_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_store16:
                run_store_instruction<uint16_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case i64_store32:
                run_store_instruction<uint32_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
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
                m_frame->stack.push(Reference { ReferenceType::Function, instruction.get_arguments<uint32_t>() });
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

                if ((uint64_t)source + count > mod->wasmFile->elements[arguments.elementIndex].functionIndexes.size() || (uint64_t)destination + count > table->elements.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                    table->elements[destination + i] = Reference { ReferenceType::Function, mod->wasmFile->elements[arguments.elementIndex].functionIndexes[source + i] };
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
            case v128_load:
                run_load_instruction<uint128_t, uint128_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load8x8_s:
                run_load_lanes_instruction<int16x8_t, int8_t, int16_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load8x8_u:
                run_load_lanes_instruction<uint16x8_t, uint8_t, uint16_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load16x4_s:
                run_load_lanes_instruction<int32x4_t, int16_t, int32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load16x4_u:
                run_load_lanes_instruction<uint32x4_t, uint16_t, uint32_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load32x2_s:
                run_load_lanes_instruction<int64x2_t, int32_t, int64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_load32x2_u:
                run_load_lanes_instruction<uint64x2_t, uint32_t, uint64_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_store:
                run_store_instruction<uint128_t, uint128_t>(instruction.get_arguments<WasmFile::MemArg>());
                break;
            case v128_const:
                m_frame->stack.push(instruction.get_arguments<uint128_t>());
                break;
            case i8x16_shuffle: {
                const uint8x16_t& arg = instruction.get_arguments<uint8x16_t>();
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
            case i8x16_swizzle:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_swizzle>();
                break;
            case i8x16_splat:
                run_unary_operation<uint32_t, operation_vector_broadcast<uint8x16_t>>();
                break;
            case i16x8_splat:
                run_unary_operation<uint32_t, operation_vector_broadcast<uint16x8_t>>();
                break;
            case i32x4_splat:
                run_unary_operation<uint32_t, operation_vector_broadcast<uint32x4_t>>();
                break;
            case i64x2_splat:
                run_unary_operation<uint64_t, operation_vector_broadcast<uint64x2_t>>();
                break;
            case f32x4_splat:
                run_unary_operation<float, operation_vector_broadcast<float32x4_t>>();
                break;
            case f64x2_splat:
                run_unary_operation<double, operation_vector_broadcast<float64x2_t>>();
                break;
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
            case i8x16_eq:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_eq>();
                break;
            case i8x16_ne:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_ne>();
                break;
            case i8x16_lt_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_lt>();
                break;
            case i8x16_lt_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_lt>();
                break;
            case i8x16_gt_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_gt>();
                break;
            case i8x16_gt_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_gt>();
                break;
            case i8x16_le_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_le>();
                break;
            case i8x16_le_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_le>();
                break;
            case i8x16_ge_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_ge>();
                break;
            case i8x16_ge_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_ge>();
                break;
            case i16x8_eq:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_eq>();
                break;
            case i16x8_ne:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_ne>();
                break;
            case i16x8_lt_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_lt>();
                break;
            case i16x8_lt_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_lt>();
                break;
            case i16x8_gt_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_gt>();
                break;
            case i16x8_gt_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_gt>();
                break;
            case i16x8_le_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_le>();
                break;
            case i16x8_le_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_le>();
                break;
            case i16x8_ge_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_ge>();
                break;
            case i16x8_ge_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_ge>();
                break;
            case i32x4_eq:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_eq>();
                break;
            case i32x4_ne:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_ne>();
                break;
            case i32x4_lt_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_lt>();
                break;
            case i32x4_lt_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_lt>();
                break;
            case i32x4_gt_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_gt>();
                break;
            case i32x4_gt_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_gt>();
                break;
            case i32x4_le_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_le>();
                break;
            case i32x4_le_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_le>();
                break;
            case i32x4_ge_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_ge>();
                break;
            case i32x4_ge_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_ge>();
                break;
            case f32x4_eq:
                run_binary_operation<float32x4_t, float32x4_t, operation_eq>();
                break;
            case f32x4_ne:
                run_binary_operation<float32x4_t, float32x4_t, operation_ne>();
                break;
            case f32x4_lt:
                run_binary_operation<float32x4_t, float32x4_t, operation_lt>();
                break;
            case f32x4_gt:
                run_binary_operation<float32x4_t, float32x4_t, operation_gt>();
                break;
            case f32x4_le:
                run_binary_operation<float32x4_t, float32x4_t, operation_le>();
                break;
            case f32x4_ge:
                run_binary_operation<float32x4_t, float32x4_t, operation_ge>();
                break;
            case f64x2_eq:
                run_binary_operation<float64x2_t, float64x2_t, operation_eq>();
                break;
            case f64x2_ne:
                run_binary_operation<float64x2_t, float64x2_t, operation_ne>();
                break;
            case f64x2_lt:
                run_binary_operation<float64x2_t, float64x2_t, operation_lt>();
                break;
            case f64x2_gt:
                run_binary_operation<float64x2_t, float64x2_t, operation_gt>();
                break;
            case f64x2_le:
                run_binary_operation<float64x2_t, float64x2_t, operation_le>();
                break;
            case f64x2_ge:
                run_binary_operation<float64x2_t, float64x2_t, operation_ge>();
                break;
            case v128_not:
                run_unary_operation<uint128_t, operation_not>();
                break;
            case v128_and:
                run_binary_operation<uint128_t, uint128_t, operation_and>();
                break;
            case v128_andnot:
                run_binary_operation<uint128_t, uint128_t, operation_andnot>();
                break;
            case v128_or:
                run_binary_operation<uint128_t, uint128_t, operation_or>();
                break;
            case v128_xor:
                run_binary_operation<uint128_t, uint128_t, operation_xor>();
                break;
            case v128_bitselect: {
                uint128_t mask = m_frame->stack.pop_as<uint128_t>();
                uint128_t falseVector = m_frame->stack.pop_as<uint128_t>();
                uint128_t trueVector = m_frame->stack.pop_as<uint128_t>();
                m_frame->stack.push((trueVector & mask) | (falseVector & ~mask));
                break;
            }
            case v128_any_true:
                m_frame->stack.push((uint32_t)(m_frame->stack.pop_as<uint128_t>() != 0));
                break;
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
            case i8x16_abs:
                run_unary_operation<int8x16_t, operation_vector_abs>();
                break;
            case i8x16_neg:
                run_unary_operation<int8x16_t, operation_neg>();
                break;
            case i8x16_all_true:
                run_unary_operation<uint8x16_t, operation_all_true>();
                break;
            case i8x16_bitmask:
                run_unary_operation<uint8x16_t, operation_bitmask>();
                break;
            case f32x4_ceil:
                run_unary_operation<float32x4_t, operation_vector_ceil>();
                break;
            case f32x4_floor:
                run_unary_operation<float32x4_t, operation_vector_floor>();
                break;
            case f32x4_trunc:
                run_unary_operation<float32x4_t, operation_vector_trunc>();
                break;
            case f32x4_nearest:
                run_unary_operation<float32x4_t, operation_vector_nearest>();
                break;
            case i8x16_shl:
                run_binary_operation<uint8x16_t, uint32_t, operation_vector_shl>();
                break;
            case i8x16_shr_s:
                run_binary_operation<int8x16_t, uint32_t, operation_vector_shr>();
                break;
            case i8x16_shr_u:
                run_binary_operation<uint8x16_t, uint32_t, operation_vector_shr>();
                break;
            case i8x16_add:
                run_binary_operation<uint8x16_t, int8x16_t, operation_add>();
                break;
            case i8x16_sub:
                run_binary_operation<uint8x16_t, int8x16_t, operation_sub>();
                break;
            case f64x2_ceil:
                run_unary_operation<float64x2_t, operation_vector_ceil>();
                break;
            case f64x2_floor:
                run_unary_operation<float64x2_t, operation_vector_floor>();
                break;
            case i8x16_min_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_vector_min>();
                break;
            case i8x16_min_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_min>();
                break;
            case i8x16_max_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_vector_max>();
                break;
            case i8x16_max_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_max>();
                break;
            case f64x2_trunc:
                run_unary_operation<float64x2_t, operation_vector_trunc>();
                break;
            case i16x8_abs:
                run_unary_operation<int16x8_t, operation_vector_abs>();
                break;
            case i16x8_neg:
                run_unary_operation<int16x8_t, operation_neg>();
                break;
            case i16x8_q15mulr_sat_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_vector_q15mulr_sat>();
                break;
            case i16x8_all_true:
                run_unary_operation<uint16x8_t, operation_all_true>();
                break;
            case i16x8_bitmask:
                run_unary_operation<uint16x8_t, operation_bitmask>();
                break;
            case i16x8_extend_low_i8x16_s:
                run_vector_extend<int8x16_t, int16x8_t, 0>();
                break;
            case i16x8_extend_high_i8x16_s:
                run_vector_extend<int8x16_t, int16x8_t, 8>();
                break;
            case i16x8_extend_low_i8x16_u:
                run_vector_extend<uint8x16_t, uint16x8_t, 0>();
                break;
            case i16x8_extend_high_i8x16_u:
                run_vector_extend<uint8x16_t, uint16x8_t, 8>();
                break;
            case i16x8_shl:
                run_binary_operation<uint16x8_t, uint32_t, operation_vector_shl>();
                break;
            case i16x8_shr_s:
                run_binary_operation<int16x8_t, uint32_t, operation_vector_shr>();
                break;
            case i16x8_shr_u:
                run_binary_operation<uint16x8_t, uint32_t, operation_vector_shr>();
                break;
            case i16x8_add:
                run_binary_operation<uint16x8_t, int16x8_t, operation_add>();
                break;
            case i16x8_sub:
                run_binary_operation<uint16x8_t, int16x8_t, operation_sub>();
                break;
            case f64x2_nearest:
                run_unary_operation<float64x2_t, operation_vector_nearest>();
                break;
            case i16x8_mul:
                run_binary_operation<uint16x8_t, int16x8_t, operation_mul>();
                break;
            case i16x8_min_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_vector_min>();
                break;
            case i16x8_min_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_vector_min>();
                break;
            case i16x8_max_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_vector_max>();
                break;
            case i16x8_max_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_vector_max>();
                break;
            case i16x8_extmul_low_i8x16_s:
                run_vector_extend_multiply<int8x16_t, int16x8_t, 0>();
                break;
            case i16x8_extmul_high_i8x16_s:
                run_vector_extend_multiply<int8x16_t, int16x8_t, 8>();
                break;
            case i16x8_extmul_low_i8x16_u:
                run_vector_extend_multiply<uint8x16_t, uint16x8_t, 0>();
                break;
            case i16x8_extmul_high_i8x16_u:
                run_vector_extend_multiply<uint8x16_t, uint16x8_t, 8>();
                break;
            case i32x4_abs:
                run_unary_operation<int32x4_t, operation_vector_abs>();
                break;
            case i32x4_neg:
                run_unary_operation<int32x4_t, operation_neg>();
                break;
            case i32x4_all_true:
                run_unary_operation<uint32x4_t, operation_all_true>();
                break;
            case i32x4_bitmask:
                run_unary_operation<uint32x4_t, operation_bitmask>();
                break;
            case i32x4_extend_low_i16x8_s:
                run_vector_extend<int16x8_t, int32x4_t, 0>();
                break;
            case i32x4_extend_high_i16x8_s:
                run_vector_extend<int16x8_t, int32x4_t, 4>();
                break;
            case i32x4_extend_low_i16x8_u:
                run_vector_extend<uint16x8_t, uint32x4_t, 0>();
                break;
            case i32x4_extend_high_i16x8_u:
                run_vector_extend<uint16x8_t, uint32x4_t, 4>();
                break;
            case i32x4_shl:
                run_binary_operation<uint32x4_t, uint32_t, operation_vector_shl>();
                break;
            case i32x4_shr_s:
                run_binary_operation<int32x4_t, uint32_t, operation_vector_shr>();
                break;
            case i32x4_shr_u:
                run_binary_operation<uint32x4_t, uint32_t, operation_vector_shr>();
                break;
            case i32x4_add:
                run_binary_operation<uint32x4_t, int32x4_t, operation_add>();
                break;
            case i32x4_sub:
                run_binary_operation<uint32x4_t, int32x4_t, operation_sub>();
                break;
            case i32x4_mul:
                run_binary_operation<uint32x4_t, int32x4_t, operation_mul>();
                break;
            case i32x4_min_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_vector_min>();
                break;
            case i32x4_min_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_vector_min>();
                break;
            case i32x4_max_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_vector_max>();
                break;
            case i32x4_max_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_vector_max>();
                break;
            case i32x4_extmul_low_i16x8_s:
                run_vector_extend_multiply<int16x8_t, int32x4_t, 0>();
                break;
            case i32x4_extmul_high_i16x8_s:
                run_vector_extend_multiply<int16x8_t, int32x4_t, 4>();
                break;
            case i32x4_extmul_low_i16x8_u:
                run_vector_extend_multiply<uint16x8_t, uint32x4_t, 0>();
                break;
            case i32x4_extmul_high_i16x8_u:
                run_vector_extend_multiply<uint16x8_t, uint32x4_t, 4>();
                break;
            case i64x2_abs:
                run_unary_operation<int64x2_t, operation_vector_abs>();
                break;
            case i64x2_neg:
                run_unary_operation<int64x2_t, operation_neg>();
                break;
            case i64x2_all_true:
                run_unary_operation<uint64x2_t, operation_all_true>();
                break;
            case i64x2_bitmask:
                run_unary_operation<uint64x2_t, operation_bitmask>();
                break;
            case i64x2_extend_low_i32x4_s:
                run_vector_extend<int32x4_t, int64x2_t, 0>();
                break;
            case i64x2_extend_high_i32x4_s:
                run_vector_extend<int32x4_t, int64x2_t, 2>();
                break;
            case i64x2_extend_low_i32x4_u:
                run_vector_extend<uint32x4_t, uint64x2_t, 0>();
                break;
            case i64x2_extend_high_i32x4_u:
                run_vector_extend<uint32x4_t, uint64x2_t, 2>();
                break;
            case i64x2_shl:
                run_binary_operation<uint64x2_t, uint32_t, operation_vector_shl>();
                break;
            case i64x2_shr_s:
                run_binary_operation<int64x2_t, uint32_t, operation_vector_shr>();
                break;
            case i64x2_shr_u:
                run_binary_operation<uint64x2_t, uint32_t, operation_vector_shr>();
                break;
            case i64x2_add:
                run_binary_operation<uint64x2_t, int64x2_t, operation_add>();
                break;
            case i64x2_sub:
                run_binary_operation<uint64x2_t, int64x2_t, operation_sub>();
                break;
            case i64x2_mul:
                run_binary_operation<uint64x2_t, int64x2_t, operation_mul>();
                break;
            case i64x2_eq:
                run_binary_operation<uint64x2_t, int64x2_t, operation_eq>();
                break;
            case i64x2_ne:
                run_binary_operation<uint64x2_t, int64x2_t, operation_ne>();
                break;
            case i64x2_lt_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_lt>();
                break;
            case i64x2_gt_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_gt>();
                break;
            case i64x2_le_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_le>();
                break;
            case i64x2_ge_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_ge>();
                break;
            case i64x2_extmul_low_i32x4_s:
                run_vector_extend_multiply<int32x4_t, int64x2_t, 0>();
                break;
            case i64x2_extmul_high_i32x4_s:
                run_vector_extend_multiply<int32x4_t, int64x2_t, 2>();
                break;
            case i64x2_extmul_low_i32x4_u:
                run_vector_extend_multiply<uint32x4_t, uint64x2_t, 0>();
                break;
            case i64x2_extmul_high_i32x4_u:
                run_vector_extend_multiply<uint32x4_t, uint64x2_t, 2>();
                break;
            case f32x4_abs:
                run_unary_operation<float32x4_t, operation_vector_abs>();
                break;
            case f32x4_neg:
                run_unary_operation<float32x4_t, operation_neg>();
                break;
            case f32x4_add:
                run_binary_operation<float32x4_t, float32x4_t, operation_add>();
                break;
            case f32x4_sub:
                run_binary_operation<float32x4_t, float32x4_t, operation_sub>();
                break;
            case f32x4_mul:
                run_binary_operation<float32x4_t, float32x4_t, operation_mul>();
                break;
            case f32x4_div:
                run_binary_operation<float32x4_t, float32x4_t, operation_div>();
                break;
            case f32x4_min:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_min>();
                break;
            case f32x4_max:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_max>();
                break;
            case f32x4_pmin:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_min>();
                break;
            case f32x4_pmax:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_max>();
                break;
            case f64x2_abs:
                run_unary_operation<float64x2_t, operation_vector_abs>();
                break;
            case f64x2_neg:
                run_unary_operation<float64x2_t, operation_neg>();
                break;
            case f64x2_add:
                run_binary_operation<float64x2_t, float64x2_t, operation_add>();
                break;
            case f64x2_sub:
                run_binary_operation<float64x2_t, float64x2_t, operation_sub>();
                break;
            case f64x2_mul:
                run_binary_operation<float64x2_t, float64x2_t, operation_mul>();
                break;
            case f64x2_div:
                run_binary_operation<float64x2_t, float32x4_t, operation_div>();
                break;
            case f64x2_min:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_min>();
                break;
            case f64x2_max:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_max>();
                break;
            case f64x2_pmin:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_min>();
                break;
            case f64x2_pmax:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_max>();
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

    clean_up_frame();

    return returnValues;
}

Ref<Module> VM::get_registered_module(const std::string& name)
{
    return m_registered_modules[name];
}

Value VM::run_bare_code_returning(Ref<Module> mod, const std::vector<Instruction>& instructions, Type returnType)
{
    uint32_t ip = 0;
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
                std::println(std::cerr, "Error: Unknown or disallowed in bare code opcode {:#x}", static_cast<uint32_t>(instruction.opcode));
                throw Trap();
        }
    }

#ifdef DEBUG_BUILD
    if (stack.size() != 1)
        throw Trap();
#endif

    Value value = stack.pop();
#ifdef DEBUG_BUILD
    if (get_value_type(value) != returnType)
        throw Trap();
#endif

    return value;
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

    m_frame->stack.push((StackType)value);
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

void VM::branch_to_label(uint32_t index)
{
    // FIXME: Does this need to be a loop
    Label label;
    for (uint32_t i = 0; i < index + 1; i++)
    {
        label = m_frame->label_stack.back();
        m_frame->label_stack.pop_back();
    }

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

template <IsVector VectorType, typename ActualType, typename LaneType>
void VM::run_load_lanes_instruction(const WasmFile::MemArg& memArg)
{
    constexpr auto laneCount = sizeof(VectorType) / sizeof(VectorElement<VectorType>);
    auto memory = m_frame->mod->get_memory(memArg.memoryIndex);

    uint32_t address = m_frame->stack.pop_as<uint32_t>();

    if ((uint64_t)address + memArg.offset + (laneCount * sizeof(ActualType)) > memory->size * WASM_PAGE_SIZE)
        throw Trap();

    VectorType vector;
    for (size_t i = 0; i < laneCount; i++)
    {
        ActualType value {};
        memcpy(&memory->data[address + memArg.offset + (i * sizeof(ActualType))], &value, sizeof(ActualType));
        vector[i] = (LaneType)value;
    }
    m_frame->stack.push(vector);
}

template <IsVector SourceVectorType, IsVector DestinationVectorType, size_t SourceOffset>
void VM::run_vector_extend()
{
    // TODO: Find a SIMD instruction way to do this
    constexpr auto laneCount = sizeof(DestinationVectorType) / sizeof(VectorElement<DestinationVectorType>);
    SourceVectorType vector = m_frame->stack.pop_as<SourceVectorType>();
    DestinationVectorType result {};
    for (size_t i = 0; i < laneCount; i++)
        result[i] = vector[i + SourceOffset];
    m_frame->stack.push(result);
}

template <IsVector SourceVectorType, IsVector DestinationVectorType, size_t SourceOffset>
void VM::run_vector_extend_multiply()
{
    // TODO: Find a SIMD instruction way to do this
    constexpr auto laneCount = sizeof(DestinationVectorType) / sizeof(VectorElement<DestinationVectorType>);
    SourceVectorType a = m_frame->stack.pop_as<SourceVectorType>();
    SourceVectorType b = m_frame->stack.pop_as<SourceVectorType>();
    DestinationVectorType result {};
    for (size_t i = 0; i < laneCount; i++)
        result[i] = a[i + SourceOffset] * b[i + SourceOffset];
    m_frame->stack.push(result);
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
