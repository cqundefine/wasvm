#pragma once

#include "Stream/Stream.h"
#include "VM/Type.h"
#include <cstdint>
#include <optional>

struct Instruction;

namespace WasmFile
{
    constexpr uint32_t WASM_SIGNATURE = 0x6d736100;
    constexpr uint32_t MAX_WASM_PAGES = 65536;

    enum class Section
    {
        Custom = 0,
        Type = 1,
        Import = 2,
        Function = 3,
        Table = 4,
        Memory = 5,
        Global = 6,
        Export = 7,
        Start = 8,
        Element = 9,
        Code = 10,
        Data = 11,
        DataCount = 12,
    };

    class InvalidWASMException
    {
    public:
        InvalidWASMException(std::string_view reason)
            : m_reason(reason)
        {
        }

        std::string_view reason() const { return m_reason; }

    private:
        std::string m_reason;
    };

    struct Limits
    {
        uint32_t min;
        std::optional<uint32_t> max;

        static Limits read_from_stream(Stream& stream);

        bool fits_within(const Limits& other) const
        {
            return min >= other.min && (!other.max.has_value() || (max.has_value() && max.value() <= other.max.value()));
        }
    };

    struct MemArg
    {
        uint32_t align;
        uint32_t offset;
        uint32_t memoryIndex;

        static MemArg read_from_stream(Stream& stream);
    };

    struct FunctionType
    {
        std::vector<Type> params;
        std::vector<Type> returns;

        static FunctionType read_from_stream(Stream& stream);

        bool operator==(const FunctionType& other) const
        {
            return params == other.params && returns == other.returns;
        }
    };

    enum class ImportType
    {
        Function,
        Table,
        Memory,
        Global
    };

    enum class GlobalMutability
    {
        Constant = 0,
        Variable = 1,
    };

    struct Import
    {
        ImportType type;
        std::string environment;
        std::string name;

        // FIXME: Make this a union
        uint32_t functionTypeIndex;

        Type tableRefType;
        Limits tableLimits;

        Limits memoryLimits;

        Type globalType;
        GlobalMutability globalMutability;

        static Import read_from_stream(Stream& stream);
    };

    struct Table
    {
        Type refType;
        Limits limits;

        static Table read_from_stream(Stream& stream);
    };

    struct Memory
    {
        Limits limits;

        static Memory read_from_stream(Stream& stream);
    };

    struct Global
    {
        Type type;
        GlobalMutability mutability;
        std::vector<Instruction> initCode;

        static Global read_from_stream(Stream& stream);
    };

    struct Export
    {
        std::string name;
        ImportType type;
        uint32_t index;

        static Export read_from_stream(Stream& stream);
    };

    enum class ElementMode
    {
        Passive,
        Active,
        Declarative
    };

    struct Element
    {
        uint32_t table;
        std::vector<Instruction> expr;
        std::vector<uint32_t> functionIndexes;
        std::vector<std::vector<Instruction>> referencesExpr;
        ElementMode mode;
        Type valueType;

        static Element read_from_stream(Stream& stream);
    };

    struct Local
    {
        uint32_t count;
        Type type;

        static Local read_from_stream(Stream& stream);
    };

    struct Code
    {
        std::vector<Type> locals;
        std::vector<Instruction> instructions;

        static Code read_from_stream(Stream& stream);
    };

    struct Data
    {
        uint32_t type;
        uint32_t memoryIndex;
        std::vector<Instruction> expr;
        std::vector<uint8_t> data;
        ElementMode mode;

        static Data read_from_stream(Stream& stream);
    };

    struct WasmFile
    {
        std::vector<FunctionType> functionTypes;
        std::vector<Import> imports;
        std::vector<uint32_t> functionTypeIndexes;
        std::vector<Table> tables;
        std::vector<Memory> memories;
        std::vector<Global> globals;
        std::vector<Export> exports;
        std::optional<uint32_t> startFunction;
        std::vector<Element> elements;
        std::vector<Code> codeBlocks;
        std::vector<Data> dataBlocks;
        std::optional<uint32_t> dataCount;

        static Ref<WasmFile> read_from_stream(Stream& stream, bool runValidator = true);

        std::optional<Export> find_export_by_name(std::string_view name);

        uint32_t get_import_count_of_type(ImportType type);
    };

    struct BlockType
    {
        std::optional<Type> type;
        uint64_t index;

        static BlockType read_from_stream(Stream& stream);

        std::vector<Type> get_param_types(Ref<WasmFile> wasmFile) const;
        std::vector<Type> get_return_types(Ref<WasmFile> wasmFile) const;
    };
}
