#pragma once

#include <Opcode.h>
#include <Stream.h>
#include <Type.h>
#include <Value.h>
#include <WasmFile.h>
#include <optional>
#include <variant>
#include <vector>

struct BlockLoopArguments
{
    WasmFile::BlockType blockType;
    Label label;
};

struct IfArguments
{
    WasmFile::BlockType blockType;
    Label endLabel;
    std::optional<uint32_t> elseLocation;
};

struct BranchTableArguments
{
    std::vector<uint32_t> labels;
    uint32_t defaultLabel;
};

struct CallIndirectArguments
{
    uint32_t typeIndex;
    uint32_t tableIndex;
};

struct MemoryInitArguments
{
    uint32_t dataIndex;
    uint32_t memoryIndex;
};

struct MemoryCopyArguments
{
    uint32_t source;
    uint32_t destination;
};

struct TableInitArguments
{
    uint32_t elementIndex;
    uint32_t tableIndex;
};

struct TableCopyArguments
{
    uint32_t destination;
    uint32_t source;
};

struct LoadStoreLaneArguments
{
    WasmFile::MemArg memArg;
    uint8_t lane;
};

struct NoneArguments
{
};

struct Instruction
{
    Opcode opcode;
    std::variant<NoneArguments, BlockLoopArguments, IfArguments, BranchTableArguments, CallIndirectArguments, MemoryInitArguments, MemoryCopyArguments, TableInitArguments, TableCopyArguments, LoadStoreLaneArguments, std::vector<uint8_t>, WasmFile::MemArg, Type, Label, uint8_t, uint32_t, uint64_t, float, double, uint128_t, uint8x16_t> arguments;

    template <typename T>
    T& get_arguments()
    {
        return std::get<T>(arguments);
    }

    template <typename T>
    const T& get_arguments() const
    {
        return std::get<T>(arguments);
    }
};

std::vector<Instruction> parse(Stream& stream, Ref<WasmFile::WasmFile> wasmFile);
