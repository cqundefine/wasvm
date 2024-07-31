#pragma once

#include <Stream.h>
#include <exception>
#include <map>
#include <optional>
#include <vector>

constexpr uint32_t WASM_SIGNATURE = 0x6d736100;

struct Instruction;

enum Section
{
    CustomSection = 0,
    TypeSection = 1,
    ImportSection = 2,
    FunctionSection = 3,
    TableSection = 4,
    MemorySection = 5,
    GlobalSection = 6,
    ExportSection = 7,
    StartSection = 8,
    ElementSection = 9,
    CodeSection = 10,
    DataSection = 11,
    DataCountSection = 12,
};

struct InvalidWASMException : public std::exception
{
    const char* what() const throw()
    {
        return "InvalidWASMException";
    }
};

struct Limits
{
    uint32_t min;
    uint32_t max;

    static Limits read_from_stream(Stream& stream);
};

struct MemArg
{
    uint32_t align;
    uint32_t offset;

    static MemArg read_from_stream(Stream& stream);
};

struct FunctionType
{
    std::vector<uint8_t> params;
    std::vector<uint8_t> returns;

    static FunctionType read_from_stream(Stream& stream);

    inline bool operator==(const FunctionType& other) const
    {
        return params == other.params && returns == other.returns;
    }
};

struct Import
{
    std::string environment;
    std::string name;
    uint32_t index;

    static Import read_from_stream(Stream& stream);
};

struct Table
{
    uint8_t refType;
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
    uint8_t type;
    uint8_t mut;
    std::vector<Instruction> initCode;

    static Global read_from_stream(Stream& stream);
};

struct Export
{
    std::string name;
    uint8_t type;
    uint32_t index;

    static Export read_from_stream(Stream& stream);
};

struct Element
{
    uint32_t type;
    uint32_t table;
    std::vector<Instruction> expr;
    std::vector<uint32_t> functionIndexes;

    static Element read_from_stream(Stream& stream);
};

struct Local
{
    uint32_t count;
    uint8_t type;

    static Local read_from_stream(Stream& stream);
};

struct Code
{
    std::vector<Local> locals;
    std::vector<Instruction> instructions;

    static Code read_from_stream(Stream& stream);
};

struct Data
{
    std::vector<Instruction> expr;
    std::vector<uint8_t> data;

    static Data read_from_stream(Stream& stream);
};

struct WasmFile
{
    std::vector<FunctionType> functionTypes;
    std::vector<Import> imports;
    std::map<uint32_t, uint32_t> functionTypeIndexes;
    std::vector<Table> tables;
    std::vector<Memory> memories;
    std::vector<Global> globals;
    std::vector<Export> exports;
    std::vector<Element> elements;
    std::map<uint32_t, Code> codeBlocks;
    std::vector<Data> dataBlocks;

    static WasmFile read_from_stream(Stream& stream);

    Export find_export_by_name(const std::string& name);
};

struct BlockType
{
    std::optional<uint8_t> type;
    uint64_t index;

    static BlockType read_from_stream(Stream& stream);

    std::vector<uint8_t> get_param_types(const WasmFile& wasmFile) const;
    std::vector<uint8_t> get_return_types(const WasmFile& wasmFile) const;
};
