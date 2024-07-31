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
    struct Frame
    {
        std::vector<Value> locals;
        ValueStack stack;
        uint32_t ip = 0;
    };

    static void bootup(WasmFile& file);

    static std::vector<Value> run_function(const std::string& name, const std::vector<Value>& args);
    static std::vector<Value> run_function(uint32_t index, const std::vector<Value>& args);
    static std::vector<Value> run_function_new_parser(const FunctionType& functionType, const Code& code, const std::vector<Value>& args);
    
private:
    static Value run_bare_code_returning(std::vector<Instruction> instructions, Type returnType);

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

    static std::vector<Value> run_wasi_call(const std::string& name, const std::vector<Value>& args);

    static inline WasmFile m_wasmFile;
    static inline Frame* m_frame;
    static inline std::stack<Frame*> m_frame_stack;
    static inline std::map<uint32_t, Value> m_globals;
    static inline uint8_t* m_memory;
    static inline uint32_t m_memory_size;
    static inline std::vector<std::vector<uint32_t>> m_tables;
};
