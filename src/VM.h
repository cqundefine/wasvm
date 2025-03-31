#pragma once

#include <Module.h>
#include <Parser.h>
#include <Stream.h>
#include <Util.h>
#include <Value.h>
#include <ValueStack.h>
#include <WasmFile.h>
#include <map>
#include <stack>

constexpr uint32_t WASM_PAGE_SIZE = 65536;

class VM
{
public:
    struct Frame
    {
        std::vector<Value> locals;
        ValueStack stack;
        std::vector<Label> label_stack;
        uint32_t ip = 0;
        Ref<Module> mod;

        inline Frame(Ref<Module> mod)
        {
            this->mod = mod;
        }
    };

    static Ref<Module> load_module(Ref<WasmFile::WasmFile> file, bool dont_make_current = false);
    static void register_module(const std::string& name, Ref<Module> module);

    static std::vector<Value> run_function(const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(const std::string& mod, const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(Ref<Module> mod, const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(Ref<Module> mod, uint32_t index, const std::vector<Value>& args);
    static std::vector<Value> run_function(Ref<Module> mod, Ref<Function> function, const std::vector<Value>& args);

    static Ref<Module> get_registered_module(const std::string& name);

    static Ref<Module> current_module() { return m_current_module; }
    static uint8_t* memory() { return m_current_module->get_memory(0)->data; } // FIXME: Remove this after rewriting WASI

    static Frame* frame() { return m_frame; }

    static void set_force_jit(bool force_jit) { m_force_jit = force_jit; }

private:
    static Value run_bare_code_returning(Ref<Module> mod, const std::vector<Instruction>& instructions, Type returnType);

    static void clean_up_frame();

    template <typename LhsType, typename RhsType, Value(function)(LhsType, RhsType)>
    static void run_binary_operation();
    template <typename T, Value(function)(T)>
    static void run_unary_operation();
    template <typename ActualType, typename StackType>
    static void run_load_instruction(const WasmFile::MemArg& memArg);
    template <typename ActualType, typename StackType>
    static void run_store_instruction(const WasmFile::MemArg& memArg);
    static void branch_to_label(uint32_t index);
    static void call_function(Ref<Function> function);

    template <IsVector VectorType, typename ActualType, typename LaneType>
    static void run_load_lane_instruction(const LoadStoreLaneArguments& args);
    template <IsVector VectorType, typename ActualType, typename StackType>
    static void run_store_lane_instruction(const LoadStoreLaneArguments& args);

    struct ImportLocation
    {
        Ref<Module> module;
        uint32_t index;
    };

    static ImportLocation find_import(const std::string& environment, const std::string& name, WasmFile::ImportType importType);

    static inline Frame* m_frame;
    static inline std::stack<Frame*> m_frame_stack;
    static inline Ref<Module> m_current_module;
    static inline std::map<std::string, Ref<Module>> m_registered_modules;

    static inline bool m_force_jit = false;
};
