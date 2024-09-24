#pragma once

#include <Parser.h>
#include <Stream.h>
#include <Value.h>
#include <WasmFile.h>
#include <map>
#include <stack>

constexpr uint32_t WASM_PAGE_SIZE = 65536;

class VM
{
public:
    struct Memory
    {
        uint8_t* data;
        uint32_t size;
        uint32_t max;
    };

    struct Module
    {
        WasmFile wasmFile;
        std::map<uint32_t, Value> globals;
        Memory memory;
        std::vector<std::vector<Reference>> tables;
    };

    struct Frame
    {
        std::vector<Value> locals;
        ValueStack stack;
        uint32_t ip = 0;
        Module* mod;

        inline Frame(Module* mod)
        {
            this->mod = mod;
        }
    };

    static void load_module(WasmFile& file);
    static void register_module(const std::string& name);

    static std::vector<Value> run_function(const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(const std::string& mod, const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(Module* mod, const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(Module* mod, uint32_t index, const std::vector<Value>& args);
    static std::vector<Value> run_function(Module* mod, const FunctionType& functionType, const Code& code, const std::vector<Value>& args);
    
    static Module* get_registered_module(const std::string& name);

    static Module* current_module() { return m_current_module; }
    static uint8_t* memory() { return get_memory(m_current_module).data; } // FIXME: Remove this after rewriting WASI

private:
    static Value run_bare_code_returning(Module* mod, std::vector<Instruction> instructions, Type returnType);

    static void clean_up_frame();

    template <typename LhsType, typename RhsType, Value(function)(LhsType, RhsType)>
    static void run_binary_operation();
    template <typename T, Value(function)(T)>
    static void run_unary_operation();
    template <typename ActualType, typename StackType>
    static void run_load_instruction(const MemArg& memArg);
    template <typename ActualType, typename StackType>
    static void run_store_instruction(const MemArg& memArg);
    static void branch_to_label(uint32_t index);
    static void call_function(uint32_t index);

    static Value& get_global(uint32_t index, Module* module);
    static std::vector<Reference>& get_table(uint32_t index, Module* module);
    static Memory& get_memory(Module* module);

    static inline Frame* m_frame;
    static inline std::stack<Frame*> m_frame_stack;
    static inline Module* m_current_module;
    static inline std::map<std::string, Module*> m_registered_modules;
};
