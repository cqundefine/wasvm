#pragma once

#include "Label.h"
#include "Module.h"
#include "Util/StringMap.h"
#include "Util/Util.h"
#include "Value.h"
#include "ValueStack.h"
#include "WasmFile/Parser.h"
#include "WasmFile/WasmFile.h"
#include <cstdint>
#include <span>
#include <vector>

constexpr uint64_t WASM_PAGE_SIZE = 65536;
constexpr uint32_t MAX_FRAME_STACK_SIZE = 256;

class VM
{
    friend class WASIModule;

public:
    struct Frame
    {
        std::vector<Value> locals;
        ValueStack stack;
        uint32_t ip = 0;
        Ref<RealModule> mod;

        Frame(Ref<RealModule> mod)
        {
            this->mod = mod;
        }
    };

    static Ref<RealModule> load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current = false);
    static void register_module(const std::string& name, Ref<Module> module);

    static std::vector<Value> run_function(const std::string& name, std::span<const Value> args);
    static std::vector<Value> run_function(const std::string& mod, const std::string& name, std::span<const Value> args);
    static std::vector<Value> run_function(Ref<Module> mod, const std::string& name, std::span<const Value> args);
    static std::vector<Value> run_function(Ref<RealModule> mod, const RealFunction* function, std::span<const Value> args);

    static Ref<Module> get_registered_module(const std::string& name);

    static Ref<Module> current_module() { return m_current_module; }

private:
    static Value run_bare_code(Ref<RealModule> mod, std::span<const Instruction> instructions);

    static Memory* get_current_frame_memory_0();

    template <typename LhsType, typename RhsType, Value(function)(LhsType, RhsType)>
    static void run_binary_operation();
    template <typename T, Value(function)(T)>
    static void run_unary_operation();
    template <typename ActualType, IsValueType StackType>
    static void run_load_instruction(const WasmFile::MemArg& memArg);
    template <typename ActualType, IsValueType StackType>
    static void run_store_instruction(const WasmFile::MemArg& memArg);
    static void branch_to_label(Label label);
    static void call_function(Ref<Function> function);

    template <IsVector VectorType, bool Zero>
    static void run_load_vector_element_instruction(const WasmFile::MemArg& megArg);
    template <IsVector VectorType, typename ActualType, typename LaneType>
    static void run_load_lane_instruction(const LoadStoreLaneArguments& args);
    template <IsVector VectorType, typename ActualType, typename StackType>
    static void run_store_lane_instruction(const LoadStoreLaneArguments& args);

    template <HasAddressType Structure>
    static uint64_t pop_address(const Structure* structure);
    template <HasAddressType Structure>
    static Value to_address(uint64_t value, const Structure* structure);

    struct ImportLocation
    {
        Ref<Module> module;
        std::variant<Ref<Function>, Ref<Table>, Ref<Memory>, Ref<Global>> imported;
    };

    static ImportLocation find_import(std::string_view environment, std::string_view name, WasmFile::ImportType type);

    static inline Frame* m_frame;
    static inline Stack<Frame*> m_frame_stack;
    static inline size_t m_next_module_id = 0;
    static inline Ref<Module> m_current_module;
    static inline StringMap<Ref<Module>> m_registered_modules;
};
