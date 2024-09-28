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

struct NoneArguments
{

};

struct Instruction
{
    Opcode opcode;
    std::variant<NoneArguments, BlockLoopArguments, IfArguments, BranchTableArguments, CallIndirectArguments, MemoryInitArguments, MemoryCopyArguments, TableInitArguments, TableCopyArguments, std::vector<uint8_t>, WasmFile::MemArg, Type, Label, uint32_t, uint64_t, float, double> arguments;
};

std::vector<Instruction> parse(Stream& stream, Ref<WasmFile::WasmFile> wasmFile);
