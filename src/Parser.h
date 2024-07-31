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
    BlockType blockType;
    Label label;
};

struct IfArguments
{
    BlockType blockType;
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

struct NoneArguments
{

};

struct Instruction
{
    Opcode opcode;
    std::variant<NoneArguments, BlockLoopArguments, IfArguments, BranchTableArguments, CallIndirectArguments, MemArg, Type, Label, uint32_t, uint64_t, float, double> arguments;
};

std::vector<Instruction> parse(Stream& stream, const WasmFile& wasmFile);
