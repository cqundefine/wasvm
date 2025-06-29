#pragma once

#include "Opcode.h"
#include "VM/Label.h"
#include "VM/Trap.h"
#include "WasmFile.h"
#include <cstdint>

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

struct BranchTableArgumentsPrevalidated
{
    std::vector<uint32_t> labels;
    uint32_t defaultLabel;
};

struct BranchTableArguments
{
    std::vector<Label> labels;
    Label defaultLabel;
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
    std::variant<NoneArguments, BlockLoopArguments, IfArguments, BranchTableArgumentsPrevalidated, BranchTableArguments, CallIndirectArguments, MemoryInitArguments, MemoryCopyArguments, TableInitArguments, TableCopyArguments, LoadStoreLaneArguments, std::vector<uint8_t>, WasmFile::MemArg, Type, Label, uint8_t, uint32_t, uint64_t, float, double, uint128_t, uint8x16_t> arguments;

    template <typename T>
    T& get_arguments()
    {
#ifdef DEBUG_BUILD
        if (!std::holds_alternative<T>(arguments))
            throw Trap("Tried to get an invalid type of arguments");
#endif
        return std::get<T>(arguments);
    }

    template <typename T>
    const T& get_arguments() const
    {
#ifdef DEBUG_BUILD
        if (!std::holds_alternative<T>(arguments))
            throw Trap("Tried to get an invalid type of arguments");
#endif
        return std::get<T>(arguments);
    }
};

std::vector<Instruction> parse(Stream& stream, Ref<WasmFile::WasmFile> wasmFile);
