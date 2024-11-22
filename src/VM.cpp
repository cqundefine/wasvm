#include <MemoryStream.h>
#include <Opcode.h>
#include <Operators.h>
#include <Compiler.h>
#include <Type.h>
#include <Util.h>
#include <VM.h>
#include <WASI.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

void VM::load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current)
{
    auto new_module = MakeRef<Module>();
    new_module->wasmFile = file;

    for (const auto& import : new_module->wasmFile->imports)
    {
        ImportLocation location = find_import(import.environment, import.name, import.type);
        switch (import.type)
        {
            case WasmFile::ImportType::Function:
                new_module->functions.push_back(location.module->functions[location.index]);
                break;
            case WasmFile::ImportType::Table:
                new_module->add_table(location.module->get_table(location.index));
                break;
            case WasmFile::ImportType::Memory:
                new_module->add_memory(location.module->get_memory(location.index));
                break;
            case WasmFile::ImportType::Global:
                new_module->add_global(location.module->get_global(location.index));
                break;
            default:
                assert(false);
        }
    }

    for (const auto& global : new_module->wasmFile->globals)
        new_module->add_global(MakeRef<Global>(run_bare_code_returning(new_module, global.initCode, global.type)));

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
        assert(tableInfo.limits.min <= tableInfo.limits.max);
        auto table = MakeRef<Table>();
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
            element.type = UINT32_MAX;
            element.table = UINT32_MAX;
            element.expr.clear();
            element.functionIndexes.clear();
            element.referencesExpr.clear();
        }
    }

    for (size_t i = 0; i < new_module->wasmFile->functionTypeIndexes.size(); i++)
    {
        auto function = MakeRef<Function>();

        auto functionTypeIndex = new_module->wasmFile->functionTypeIndexes[i];
        // FIXME: This should be checked while loading the file
        if (functionTypeIndex >= new_module->wasmFile->functionTypes.size())
            throw WasmFile::InvalidWASMException();

        function->type = new_module->wasmFile->functionTypes[functionTypeIndex];
        function->mod = new_module;
        function->code = new_module->wasmFile->codeBlocks[i];
        new_module->functions.push_back(function);
    }

    // FIXME: This should be checked while loading the file
    if (new_module->wasmFile->startFunction != UINT32_MAX && new_module->wasmFile->startFunction >= new_module->functions.size())
        throw WasmFile::InvalidWASMException();

    if (new_module->wasmFile->startFunction != UINT32_MAX)
        run_function(new_module, new_module->wasmFile->startFunction, {});

    if (!dont_make_current)
        m_current_module = new_module;
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

    for (uint32_t i = 0; i < args.size(); i++)
        m_frame->locals.push_back(args.at(i));

    for (const auto& local : function->code.locals)
        m_frame->locals.push_back(default_value_for_type(local));

    // try
    // {
    //     auto jitted_code = Compiler::compile(function, mod->wasmFile);
    //     Value returnValue;
    //     jitted_code(m_frame->locals.data(), &returnValue);
    //     clean_up_frame();
    //     return { returnValue };
    // }
    // catch (JITCompilationException error)
    // {
    //     fprintf(stderr, "Failed to compile JIT\n");
    //     throw Trap();
    // }

    m_frame->label_stack.push_back(Label {
        .continuation = static_cast<uint32_t>(function->code.instructions.size()),
        .arity = static_cast<uint32_t>(function->type.returns.size()),
        .stackHeight = 0 });

    while (m_frame->ip < function->code.instructions.size())
    {
        const Instruction& instruction = function->code.instructions[m_frame->ip++];

        switch (instruction.opcode)
        {
            case Opcode::unreachable:
                throw Trap();
            case Opcode::nop:
                break;
            case Opcode::block:
            case Opcode::loop: {
                const BlockLoopArguments& arguments = std::get<BlockLoopArguments>(instruction.arguments);
                Label label = arguments.label;
                label.stackHeight = static_cast<uint32_t>(m_frame->stack.size() - arguments.blockType.get_param_types(mod->wasmFile).size());
                m_frame->label_stack.push_back(label);
                break;
            }
            case Opcode::if_: {
                const IfArguments& arguments = std::get<IfArguments>(instruction.arguments);

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
            case Opcode::else_:
                m_frame->ip = std::get<Label>(instruction.arguments).continuation;
                [[fallthrough]];
            case Opcode::end:
                m_frame->label_stack.pop_back();
                break;
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
                for (size_t i = 0; i < function->type.returns.size(); i++)
                    returnValues.push_back(m_frame->stack.pop());

                std::reverse(returnValues.begin(), returnValues.end());

                for (size_t i = 0; i < returnValues.size(); i++)
                    if (get_value_type(returnValues[i]) != function->type.returns[i])
                        throw Trap();

                clean_up_frame();

                return returnValues;
            }
            case Opcode::call:
                call_function(mod->functions[std::get<uint32_t>(instruction.arguments)]);
                break;
            case Opcode::call_indirect: {
                const CallIndirectArguments& arguments = std::get<CallIndirectArguments>(instruction.arguments);

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
                m_frame->stack.push(mod->get_global(std::get<uint32_t>(instruction.arguments))->value);
                break;
            case Opcode::global_set:
                mod->get_global(std::get<uint32_t>(instruction.arguments))->value = m_frame->stack.pop();
                break;
            case Opcode::table_get: {
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                auto table = mod->get_table(std::get<uint32_t>(instruction.arguments));
                if (index >= table->elements.size())
                    throw Trap();
                m_frame->stack.push(table->elements[index]);
                break;
            }
            case Opcode::table_set: {
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t index = m_frame->stack.pop_as<uint32_t>();
                auto table = mod->get_table(std::get<uint32_t>(instruction.arguments));
                if (index >= table->elements.size())
                    throw Trap();
                table->elements[index] = value;
                break;
            }
            case Opcode::i32_load:
                run_load_instruction<uint32_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load:
                run_load_instruction<uint64_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f32_load:
                run_load_instruction<float, float>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f64_load:
                run_load_instruction<double, double>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load8_s:
                run_load_instruction<int8_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load8_u:
                run_load_instruction<uint8_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load16_s:
                run_load_instruction<int16_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_load16_u:
                run_load_instruction<uint16_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load8_s:
                run_load_instruction<int8_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load8_u:
                run_load_instruction<uint8_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load16_s:
                run_load_instruction<int16_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load16_u:
                run_load_instruction<uint16_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load32_s:
                run_load_instruction<int32_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_load32_u:
                run_load_instruction<uint32_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store:
                run_store_instruction<uint32_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store:
                run_store_instruction<uint64_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f32_store:
                run_store_instruction<float, float>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::f64_store:
                run_store_instruction<double, double>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store8:
                run_store_instruction<uint8_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i32_store16:
                run_store_instruction<uint16_t, uint32_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store8:
                run_store_instruction<uint8_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store16:
                run_store_instruction<uint16_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::i64_store32:
                run_store_instruction<uint32_t, uint64_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::memory_size:
                m_frame->stack.push(mod->get_memory(std::get<uint32_t>(instruction.arguments))->size);
                break;
            case Opcode::memory_grow: {
                auto memory = mod->get_memory(std::get<uint32_t>(instruction.arguments));

                uint32_t addPages = m_frame->stack.pop_as<uint32_t>();

                if (memory->size + addPages > std::min(memory->max, 65536u))
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
            case Opcode::i64_extend_i32_s:
                m_frame->stack.push((uint64_t)(int64_t)(int32_t)m_frame->stack.pop_as<uint32_t>());
                break;
            case Opcode::i64_extend_i32_u:
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
                const MemoryInitArguments& arguments = std::get<MemoryInitArguments>(instruction.arguments);
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
            case Opcode::data_drop: {
                WasmFile::Data& data = mod->wasmFile->dataBlocks[std::get<uint32_t>(instruction.arguments)];
                data.type = UINT32_MAX;
                data.expr.clear();
                data.data.clear();
                break;
            }
            case Opcode::memory_copy: {
                const MemoryCopyArguments& arguments = std::get<MemoryCopyArguments>(instruction.arguments);
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
            case Opcode::memory_fill: {
                auto memory = mod->get_memory(std::get<uint32_t>(instruction.arguments));

                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                uint32_t val = m_frame->stack.pop_as<uint32_t>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                if ((uint64_t)destination + count > memory->size * WASM_PAGE_SIZE)
                    throw Trap();

                memset(memory->data + destination, val, count);
                break;
            }
            case Opcode::table_init: {
                const TableInitArguments& arguments = std::get<TableInitArguments>(instruction.arguments);
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
            case Opcode::elem_drop: {
                WasmFile::Element& elem = mod->wasmFile->elements[std::get<uint32_t>(instruction.arguments)];
                elem.type = UINT32_MAX;
                elem.table = UINT32_MAX;
                elem.expr.clear();
                elem.functionIndexes.clear();
                elem.referencesExpr.clear();
                break;
            }
            case Opcode::table_copy: {
                const TableCopyArguments& arguments = std::get<TableCopyArguments>(instruction.arguments);
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
            case Opcode::table_grow: {
                uint32_t addEntries = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(std::get<uint32_t>(instruction.arguments));
                uint32_t oldSize = table->elements.size();

                Reference value = m_frame->stack.pop_as<Reference>();

                // NOTE: If the limits max is not present, it's UINT32_MAX
                if ((uint64_t)table->elements.size() + addEntries > table->max)
                {
                    m_frame->stack.push((uint32_t)-1);
                    break;
                }

                if (table->elements.size() + addEntries <= table->max)
                {
                    assert(addEntries >= 0);
                    for (uint32_t i = 0; i < addEntries; i++)
                        table->elements.push_back(value);
                }

                m_frame->stack.push(oldSize);
                break;
            }
            case Opcode::table_size:
                m_frame->stack.push((uint32_t)mod->get_table(std::get<uint32_t>(instruction.arguments))->elements.size());
                break;
            case Opcode::table_fill: {
                uint32_t count = m_frame->stack.pop_as<uint32_t>();
                Reference value = m_frame->stack.pop_as<Reference>();
                uint32_t destination = m_frame->stack.pop_as<uint32_t>();

                auto table = mod->get_table(std::get<uint32_t>(instruction.arguments));

                if (destination + count > table->elements.size())
                    throw Trap();

                for (uint32_t i = 0; i < count; i++)
                    table->elements[destination + i] = value;

                break;
            }
            case Opcode::v128_load:
                run_load_instruction<uint128_t, uint128_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::v128_store:
                run_store_instruction<uint128_t, uint128_t>(std::get<WasmFile::MemArg>(instruction.arguments));
                break;
            case Opcode::v128_const:
                m_frame->stack.push(std::get<uint128_t>(instruction.arguments));
                break;
            case Opcode::i8x16_shuffle: {
                const uint8x16_t& arg = std::get<uint8x16_t>(instruction.arguments);
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
            case Opcode::i8x16_swizzle:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_swizzle>();
                break;
            case Opcode::i8x16_splat:
                m_frame->stack.push(vector_broadcast<uint8x16_t>(m_frame->stack.pop_as<uint32_t>()));
                break;
            case Opcode::i16x8_splat:
                m_frame->stack.push(vector_broadcast<uint16x8_t>(m_frame->stack.pop_as<uint32_t>()));
                break;
            case Opcode::i32x4_splat:
                m_frame->stack.push(vector_broadcast<uint32x4_t>(m_frame->stack.pop_as<uint32_t>()));
                break;
            case Opcode::i64x2_splat:
                m_frame->stack.push(vector_broadcast<uint64x2_t>(m_frame->stack.pop_as<uint64_t>()));
                break;
            case Opcode::f32x4_splat:
                m_frame->stack.push(vector_broadcast<float32x4_t>(m_frame->stack.pop_as<float>()));
                break;
            case Opcode::f64x2_splat:
                m_frame->stack.push(vector_broadcast<float64x2_t>(m_frame->stack.pop_as<double>()));
                break;
            case Opcode::i8x16_extract_lane_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<int8x16_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i8x16_extract_lane_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<uint8x16_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i8x16_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint8x16_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::i16x8_extract_lane_s:
                m_frame->stack.push((uint32_t)(int32_t)m_frame->stack.pop_as<int16x8_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i16x8_extract_lane_u:
                m_frame->stack.push((uint32_t)m_frame->stack.pop_as<uint16x8_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i16x8_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint16x8_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::i32x4_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<uint32x4_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i32x4_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint32_t>();
                auto vector = m_frame->stack.pop_as<uint32x4_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::i64x2_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<uint64x2_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::i64x2_replace_lane: {
                auto lane = m_frame->stack.pop_as<uint64_t>();
                auto vector = m_frame->stack.pop_as<uint64x2_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::f32x4_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<float32x4_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::f32x4_replace_lane: {
                auto lane = m_frame->stack.pop_as<float>();
                auto vector = m_frame->stack.pop_as<float32x4_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::f64x2_extract_lane:
                m_frame->stack.push(m_frame->stack.pop_as<float64x2_t>()[std::get<uint8_t>(instruction.arguments)]);
                break;
            case Opcode::f64x2_replace_lane: {
                auto lane = m_frame->stack.pop_as<double>();
                auto vector = m_frame->stack.pop_as<float64x2_t>();
                vector[std::get<uint8_t>(instruction.arguments)] = lane;
                m_frame->stack.push(vector);
                break;
            }
            case Opcode::i8x16_eq:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_eq>();
                break;
            case Opcode::i8x16_ne:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_ne>();
                break;
            case Opcode::i8x16_lt_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_lt>();
                break;
            case Opcode::i8x16_lt_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_lt>();
                break;
            case Opcode::i8x16_gt_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_gt>();
                break;
            case Opcode::i8x16_gt_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_gt>();
                break;
            case Opcode::i8x16_le_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_le>();
                break;
            case Opcode::i8x16_le_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_le>();
                break;
            case Opcode::i8x16_ge_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_ge>();
                break;
            case Opcode::i8x16_ge_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_ge>();
                break;
            case Opcode::i16x8_eq:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_eq>();
                break;
            case Opcode::i16x8_ne:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_ne>();
                break;
            case Opcode::i16x8_lt_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_lt>();
                break;
            case Opcode::i16x8_lt_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_lt>();
                break;
            case Opcode::i16x8_gt_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_gt>();
                break;
            case Opcode::i16x8_gt_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_gt>();
                break;
            case Opcode::i16x8_le_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_le>();
                break;
            case Opcode::i16x8_le_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_le>();
                break;
            case Opcode::i16x8_ge_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_ge>();
                break;
            case Opcode::i16x8_ge_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_ge>();
                break;
            case Opcode::i32x4_eq:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_eq>();
                break;
            case Opcode::i32x4_ne:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_ne>();
                break;
            case Opcode::i32x4_lt_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_lt>();
                break;
            case Opcode::i32x4_lt_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_lt>();
                break;
            case Opcode::i32x4_gt_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_gt>();
                break;
            case Opcode::i32x4_gt_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_gt>();
                break;
            case Opcode::i32x4_le_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_le>();
                break;
            case Opcode::i32x4_le_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_le>();
                break;
            case Opcode::i32x4_ge_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_ge>();
                break;
            case Opcode::i32x4_ge_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_ge>();
                break;
            case Opcode::f32x4_eq:
                run_binary_operation<float32x4_t, float32x4_t, operation_eq>();
                break;
            case Opcode::f32x4_ne:
                run_binary_operation<float32x4_t, float32x4_t, operation_ne>();
                break;
            case Opcode::f32x4_lt:
                run_binary_operation<float32x4_t, float32x4_t, operation_lt>();
                break;
            case Opcode::f32x4_gt:
                run_binary_operation<float32x4_t, float32x4_t, operation_gt>();
                break;
            case Opcode::f32x4_le:
                run_binary_operation<float32x4_t, float32x4_t, operation_le>();
                break;
            case Opcode::f32x4_ge:
                run_binary_operation<float32x4_t, float32x4_t, operation_ge>();
                break;
            case Opcode::f64x2_eq:
                run_binary_operation<float64x2_t, float64x2_t, operation_eq>();
                break;
            case Opcode::f64x2_ne:
                run_binary_operation<float64x2_t, float64x2_t, operation_ne>();
                break;
            case Opcode::f64x2_lt:
                run_binary_operation<float64x2_t, float64x2_t, operation_lt>();
                break;
            case Opcode::f64x2_gt:
                run_binary_operation<float64x2_t, float64x2_t, operation_gt>();
                break;
            case Opcode::f64x2_le:
                run_binary_operation<float64x2_t, float64x2_t, operation_le>();
                break;
            case Opcode::f64x2_ge:
                run_binary_operation<float64x2_t, float64x2_t, operation_ge>();
                break;
            case Opcode::v128_not:
                run_unary_operation<uint128_t, operation_not>();
                break;
            case Opcode::v128_and:
                run_binary_operation<uint128_t, uint128_t, operation_and>();
                break;
            case Opcode::v128_andnot:
                run_binary_operation<uint128_t, uint128_t, operation_andnot>();
                break;
            case Opcode::v128_or:
                run_binary_operation<uint128_t, uint128_t, operation_or>();
                break;
            case Opcode::v128_xor:
                run_binary_operation<uint128_t, uint128_t, operation_xor>();
                break;
            case Opcode::v128_bitselect: {
                uint128_t mask = m_frame->stack.pop_as<uint128_t>();
                uint128_t falseVector = m_frame->stack.pop_as<uint128_t>();
                uint128_t trueVector = m_frame->stack.pop_as<uint128_t>();
                m_frame->stack.push((trueVector & mask) | (falseVector & ~mask));
                break;
            }
            case Opcode::v128_any_true:
                m_frame->stack.push((uint32_t)(m_frame->stack.pop_as<uint128_t>() != 0));
                break;
            case Opcode::i8x16_abs:
                run_unary_operation<int8x16_t, operation_vector_abs>();
                break;
            case Opcode::i8x16_neg:
                run_unary_operation<int8x16_t, operation_neg>();
                break;
            case Opcode::i8x16_all_true:
                run_unary_operation<uint8x16_t, operation_all_true>();
                break;
            case Opcode::i8x16_bitmask:
                run_unary_operation<uint8x16_t, operation_bitmask>();
                break;
            case Opcode::i8x16_shl:
                run_binary_operation<uint8x16_t, uint32_t, operation_vector_shl>();
                break;
            case Opcode::i8x16_shr_s:
                run_binary_operation<int8x16_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i8x16_shr_u:
                run_binary_operation<uint8x16_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i8x16_add:
                run_binary_operation<uint8x16_t, int8x16_t, operation_add>();
                break;
            case Opcode::i8x16_sub:
                run_binary_operation<uint8x16_t, int8x16_t, operation_sub>();
                break;
            case Opcode::i8x16_min_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_vector_min>();
                break;
            case Opcode::i8x16_min_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_min>();
                break;
            case Opcode::i8x16_max_s:
                run_binary_operation<int8x16_t, int8x16_t, operation_vector_max>();
                break;
            case Opcode::i8x16_max_u:
                run_binary_operation<uint8x16_t, uint8x16_t, operation_vector_max>();
                break;
            case Opcode::i16x8_abs:
                run_unary_operation<int16x8_t, operation_vector_abs>();
                break;
            case Opcode::i16x8_neg:
                run_unary_operation<int16x8_t, operation_neg>();
                break;
            case Opcode::i16x8_all_true:
                run_unary_operation<uint16x8_t, operation_all_true>();
                break;
            case Opcode::i16x8_bitmask:
                run_unary_operation<uint16x8_t, operation_bitmask>();
                break;
            case Opcode::i16x8_shl:
                run_binary_operation<uint16x8_t, uint32_t, operation_vector_shl>();
                break;
            case Opcode::i16x8_shr_s:
                run_binary_operation<int16x8_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i16x8_shr_u:
                run_binary_operation<uint16x8_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i16x8_add:
                run_binary_operation<uint16x8_t, int16x8_t, operation_add>();
                break;
            case Opcode::i16x8_sub:
                run_binary_operation<uint16x8_t, int16x8_t, operation_sub>();
                break;
            case Opcode::i16x8_mul:
                run_binary_operation<uint16x8_t, int16x8_t, operation_mul>();
                break;
            case Opcode::i16x8_min_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_vector_min>();
                break;
            case Opcode::i16x8_min_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_vector_min>();
                break;
            case Opcode::i16x8_max_s:
                run_binary_operation<int16x8_t, int16x8_t, operation_vector_max>();
                break;
            case Opcode::i16x8_max_u:
                run_binary_operation<uint16x8_t, uint16x8_t, operation_vector_max>();
                break;
            case Opcode::i32x4_abs:
                run_unary_operation<int32x4_t, operation_vector_abs>();
                break;
            case Opcode::i32x4_neg:
                run_unary_operation<int32x4_t, operation_neg>();
                break;
            case Opcode::i32x4_all_true:
                run_unary_operation<uint32x4_t, operation_all_true>();
                break;
            case Opcode::i32x4_bitmask:
                run_unary_operation<uint32x4_t, operation_bitmask>();
                break;
            case Opcode::i32x4_shl:
                run_binary_operation<uint32x4_t, uint32_t, operation_vector_shl>();
                break;
            case Opcode::i32x4_shr_s:
                run_binary_operation<int32x4_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i32x4_shr_u:
                run_binary_operation<uint32x4_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i32x4_add:
                run_binary_operation<uint32x4_t, int32x4_t, operation_add>();
                break;
            case Opcode::i32x4_sub:
                run_binary_operation<uint32x4_t, int32x4_t, operation_sub>();
                break;
            case Opcode::i32x4_mul:
                run_binary_operation<uint32x4_t, int32x4_t, operation_mul>();
                break;
            case Opcode::i32x4_min_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_vector_min>();
                break;
            case Opcode::i32x4_min_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_vector_min>();
                break;
            case Opcode::i32x4_max_s:
                run_binary_operation<int32x4_t, int32x4_t, operation_vector_max>();
                break;
            case Opcode::i32x4_max_u:
                run_binary_operation<uint32x4_t, uint32x4_t, operation_vector_max>();
                break;
            case Opcode::i64x2_abs:
                run_unary_operation<int64x2_t, operation_vector_abs>();
                break;
            case Opcode::i64x2_neg:
                run_unary_operation<int64x2_t, operation_neg>();
                break;
            case Opcode::i64x2_all_true:
                run_unary_operation<uint64x2_t, operation_all_true>();
                break;
            case Opcode::i64x2_bitmask:
                run_unary_operation<uint64x2_t, operation_bitmask>();
                break;
            case Opcode::i64x2_shl:
                run_binary_operation<uint64x2_t, uint32_t, operation_vector_shl>();
                break;
            case Opcode::i64x2_shr_s:
                run_binary_operation<int64x2_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i64x2_shr_u:
                run_binary_operation<uint64x2_t, uint32_t, operation_vector_shr>();
                break;
            case Opcode::i64x2_add:
                run_binary_operation<uint64x2_t, int64x2_t, operation_add>();
                break;
            case Opcode::i64x2_sub:
                run_binary_operation<uint64x2_t, int64x2_t, operation_sub>();
                break;
            case Opcode::i64x2_mul:
                run_binary_operation<uint64x2_t, int64x2_t, operation_mul>();
                break;
            case Opcode::i64x2_eq:
                run_binary_operation<uint64x2_t, int64x2_t, operation_eq>();
                break;
            case Opcode::i64x2_ne:
                run_binary_operation<uint64x2_t, int64x2_t, operation_ne>();
                break;
            case Opcode::i64x2_lt_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_lt>();
                break;
            case Opcode::i64x2_gt_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_gt>();
                break;
            case Opcode::i64x2_le_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_le>();
                break;
            case Opcode::i64x2_ge_s:
                run_binary_operation<int64x2_t, int64x2_t, operation_ge>();
                break;
            case Opcode::f32x4_abs:
                run_unary_operation<float32x4_t, operation_vector_abs>();
                break;
            case Opcode::f32x4_neg:
                run_unary_operation<float32x4_t, operation_neg>();
                break;
            case Opcode::f32x4_add:
                run_binary_operation<float32x4_t, float32x4_t, operation_add>();
                break;
            case Opcode::f32x4_sub:
                run_binary_operation<float32x4_t, float32x4_t, operation_sub>();
                break;
            case Opcode::f32x4_mul:
                run_binary_operation<float32x4_t, float32x4_t, operation_mul>();
                break;
            case Opcode::f32x4_div:
                run_binary_operation<float32x4_t, float32x4_t, operation_div>();
                break;
            case Opcode::f32x4_min:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_min>();
                break;
            case Opcode::f32x4_max:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_max>();
                break;
            case Opcode::f32x4_pmin:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_min>();
                break;
            case Opcode::f32x4_pmax:
                run_binary_operation<float32x4_t, float32x4_t, operation_vector_max>();
                break;
            case Opcode::f64x2_abs:
                run_unary_operation<float64x2_t, operation_vector_abs>();
                break;
            case Opcode::f64x2_neg:
                run_unary_operation<float64x2_t, operation_neg>();
                break;
            case Opcode::f64x2_add:
                run_binary_operation<float64x2_t, float64x2_t, operation_add>();
                break;
            case Opcode::f64x2_sub:
                run_binary_operation<float64x2_t, float64x2_t, operation_sub>();
                break;
            case Opcode::f64x2_mul:
                run_binary_operation<float64x2_t, float64x2_t, operation_mul>();
                break;
            case Opcode::f64x2_div:
                run_binary_operation<float64x2_t, float32x4_t, operation_div>();
                break;
            case Opcode::f64x2_min:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_min>();
                break;
            case Opcode::f64x2_max:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_max>();
                break;
            case Opcode::f64x2_pmin:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_min>();
                break;
            case Opcode::f64x2_pmax:
                run_binary_operation<float64x2_t, float64x2_t, operation_vector_max>();
                break;
            default:
                fprintf(stderr, "Error: Unknown opcode 0x%x\n", static_cast<uint32_t>(instruction.opcode));
                throw Trap();
        }
    }

    std::vector<Value> returnValues;
    if (function->type.returns.size() > 0)
    {
        for (size_t i = function->type.returns.size() - 1; i != (size_t)-1; i--)
        {
            auto returnValue = m_frame->stack.pop();
            if (get_value_type(returnValue) != function->type.returns[i])
            {
                printf("Error: Unxpected return value on the stack: %s, expected %s\n", get_type_name(get_value_type(returnValue)).c_str(), get_type_name(function->type.returns[i]).c_str());  
                throw Trap();
            }
            returnValues.push_back(returnValue);
        }

        std::reverse(returnValues.begin(), returnValues.end());
    }

    if (m_frame->stack.size() != 0)
    {
        printf("Error: Stack not empty when running function, size is %u\n", m_frame->stack.size());
        throw Trap();
    }

    clean_up_frame();

    return returnValues;
}

Ref<Module> VM::get_registered_module(const std::string& name)
{
    return m_registered_modules[name];
}

Value VM::run_bare_code_returning(Ref<Module> mod, std::vector<Instruction> instructions, Type returnType)
{
    uint32_t ip = 0;
    ValueStack stack;

    while (ip < instructions.size())
    {
        const Instruction& instruction = instructions[ip++];

        switch (instruction.opcode)
        {
            case Opcode::end:
                if (ip < instructions.size())
                    throw Trap();
                break;
            case Opcode::global_get:
                // FIXME: Verify that the global is const
                stack.push(mod->get_global(std::get<uint32_t>(instruction.arguments))->value);
                break;
            case Opcode::i32_const:
                stack.push(std::get<uint32_t>(instruction.arguments));
                break;
            case Opcode::i64_const:
                stack.push(std::get<uint64_t>(instruction.arguments));
                break;
            case Opcode::f32_const:
                stack.push(std::get<float>(instruction.arguments));
                break;
            case Opcode::f64_const:
                stack.push(std::get<double>(instruction.arguments));
                break;
            case Opcode::ref_null:
                stack.push(default_value_for_type(std::get<Type>(instruction.arguments)));
                break;
            case Opcode::ref_func:
                stack.push(Reference { ReferenceType::Function, std::get<uint32_t>(instruction.arguments) });
                break;
            case Opcode::v128_const:
                stack.push(std::get<uint128_t>(instruction.arguments));
                break;
            default:
                fprintf(stderr, "Error: Unknown or disallowed in bare code opcode 0x%x\n", static_cast<uint32_t>(instruction.opcode));
                throw Trap();
        }
    }

    if (stack.size() != 1)
        throw Trap();

    Value value = stack.pop();
    if (get_value_type(value) != returnType)
        throw Trap();

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
    std::vector<Value> args;
    for (size_t i = 0; i < function->type.params.size(); i++)
    {
        args.push_back(m_frame->stack.pop());
    }
    std::reverse(args.begin(), args.end());

    for (size_t i = 0; i < function->type.params.size(); i++)
    {
        if (get_value_type(args.at(i)) != function->type.params.at(i))
            throw Trap();
    }

    std::vector<Value> returnedValues;
    returnedValues = run_function(function->mod, function, args);

    for (const auto& returned : returnedValues)
        m_frame->stack.push(returned);
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
