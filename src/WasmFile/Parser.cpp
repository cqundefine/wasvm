#include "Parser.h"
#include "Util/Stack.h"
#include "VM/Proposals.h"
#include <cstdint>
#include <utility>

struct BlockBeginInfo
{
    uint32_t begin;
    uint32_t arity;
    bool isFake;
};

std::vector<Instruction> parse(Stream& stream, Ref<WasmFile::WasmFile> wasmFile)
{
    std::vector<Instruction> instructions;
    Stack<BlockBeginInfo> blockBeginStack;

    while (!stream.eof())
    {
        Opcode opcode = static_cast<Opcode>(stream.read_little_endian<uint8_t>());

        switch (opcode)
        {
            using enum Opcode;
            case block: {
                WasmFile::BlockType blockType = stream.read_typed<WasmFile::BlockType>();
                blockBeginStack.push(BlockBeginInfo {
                    .begin = static_cast<uint32_t>(instructions.size()),
                    .arity = static_cast<uint32_t>(blockType.get_return_types(wasmFile).size()),
                    .isFake = false });

                instructions.push_back(Instruction { .opcode = opcode, .arguments = BlockLoopArguments { .blockType = blockType } });
                break;
            }
            case loop: {
                WasmFile::BlockType blockType = stream.read_typed<WasmFile::BlockType>();
                blockBeginStack.push(BlockBeginInfo {
                    .begin = 0,
                    .arity = 0,
                    .isFake = true });

                Label label = {
                    .continuation = static_cast<uint32_t>(instructions.size()),
                    .arity = static_cast<uint32_t>(blockType.get_param_types(wasmFile).size()),
                };

                instructions.push_back(Instruction { .opcode = opcode, .arguments = BlockLoopArguments { .blockType = blockType, .label = label } });
                break;
            }
            case if_: {
                WasmFile::BlockType blockType = stream.read_typed<WasmFile::BlockType>();
                blockBeginStack.push(BlockBeginInfo {
                    .begin = static_cast<uint32_t>(instructions.size()),
                    .arity = static_cast<uint32_t>(blockType.get_return_types(wasmFile).size()),
                    .isFake = false });

                instructions.push_back(Instruction { .opcode = opcode, .arguments = IfArguments { .blockType = blockType } });
                break;
            }
            case else_: {
                if (blockBeginStack.empty())
                    throw WasmFile::InvalidWASMException("Invalid code");

                auto begin = blockBeginStack.peek();
                auto& beginInstruction = instructions[begin.begin];

                if (!std::holds_alternative<IfArguments>(beginInstruction.arguments))
                    throw WasmFile::InvalidWASMException("Invalid code");
                beginInstruction.get_arguments<IfArguments>().elseLocation = instructions.size();

                instructions.push_back(Instruction { .opcode = opcode });
                break;
            }
            case end: {
                instructions.push_back(Instruction { .opcode = opcode });

                if (blockBeginStack.empty())
                    return instructions;

                auto begin = blockBeginStack.pop();
                auto& beginInstruction = instructions[begin.begin];

                if (begin.isFake)
                    break;

                Label label = {
                    .continuation = static_cast<uint32_t>(instructions.size()),
                    .arity = begin.arity,
                };

                if (std::holds_alternative<BlockLoopArguments>(beginInstruction.arguments))
                {
                    std::get<BlockLoopArguments>(beginInstruction.arguments).label = label;
                }
                else if (std::holds_alternative<IfArguments>(beginInstruction.arguments))
                {
                    IfArguments& arguments = std::get<IfArguments>(beginInstruction.arguments);
                    arguments.endLabel = label;
                    if (arguments.elseLocation.has_value())
                        instructions[arguments.elseLocation.value()].arguments = label;
                }
                else
                {
                    std::unreachable();
                }

                break;
            }
            case br:
            case br_if:
            case call:
            case local_get:
            case local_set:
            case local_tee:
            case global_get:
            case global_set:
            case table_get:
            case table_set:
            case ref_func:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_leb<uint32_t>() });
                break;
            case memory_size:
            case memory_grow:
                if (g_enable_multi_memory)
                    instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_leb<uint32_t>() });
                else
                {
                    if (stream.read_little_endian<uint8_t>() != 0)
                        throw WasmFile::InvalidWASMException("Invalid code");
                    instructions.push_back(Instruction { .opcode = opcode, .arguments = static_cast<uint32_t>(0) });
                }
                break;
            case br_table:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = BranchTableArgumentsPrevalidated { .labels = stream.read_vec<uint32_t>(), .defaultLabel = stream.read_leb<uint32_t>() } });
                break;
            case call_indirect:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = CallIndirectArguments {
                                                                           .typeIndex = stream.read_leb<uint32_t>(),
                                                                           .tableIndex = stream.read_leb<uint32_t>(),
                                                                       } });
                break;
            case select_typed:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_vec<uint8_t>() });
                break;
            case i32_load:
            case i64_load:
            case f32_load:
            case f64_load:
            case i32_load8_s:
            case i32_load8_u:
            case i32_load16_s:
            case i32_load16_u:
            case i64_load8_s:
            case i64_load8_u:
            case i64_load16_s:
            case i64_load16_u:
            case i64_load32_s:
            case i64_load32_u:
            case i32_store:
            case i64_store:
            case f32_store:
            case f64_store:
            case i32_store8:
            case i32_store16:
            case i64_store8:
            case i64_store16:
            case i64_store32:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_typed<WasmFile::MemArg>() });
                break;
            case i32_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = static_cast<uint32_t>(stream.read_leb<int32_t>()) });
                break;
            case i64_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = static_cast<uint64_t>(stream.read_leb<int64_t>()) });
                break;
            case f32_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_little_endian<float>() });
                break;
            case f64_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_little_endian<double>() });
                break;
            case ref_null: {
                Type type = static_cast<Type>(stream.read_little_endian<uint8_t>());
                if (!is_reference_type(type))
                    throw WasmFile::InvalidWASMException("Invalid code");
                instructions.push_back(Instruction { .opcode = opcode, .arguments = type });
                break;
            }
            case multi_byte_fc: {
                MultiByteFC secondByte = static_cast<MultiByteFC>(stream.read_leb<uint32_t>());
                Opcode realOpcode = static_cast<Opcode>(((static_cast<uint32_t>(opcode)) << 8) | static_cast<uint32_t>(secondByte));
                switch (secondByte)
                {
                    using enum MultiByteFC;
                    case memory_init:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = MemoryInitArguments { .dataIndex = stream.read_leb<uint32_t>(), .memoryIndex = stream.read_little_endian<uint8_t>() } });
                        break;
                    case memory_copy:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = MemoryCopyArguments { .source = stream.read_little_endian<uint8_t>(), .destination = stream.read_little_endian<uint8_t>() } });
                        break;
                    case table_init:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableInitArguments { .elementIndex = stream.read_leb<uint32_t>(), .tableIndex = stream.read_leb<uint32_t>() } });
                        break;
                    case table_copy:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableCopyArguments { .destination = stream.read_leb<uint32_t>(), .source = stream.read_leb<uint32_t>() } });
                        break;
                    case data_drop:
                    case memory_fill:
                    case elem_drop:
                    case table_grow:
                    case table_size:
                    case table_fill:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_leb<uint32_t>() });
                        break;
                    case i32_trunc_sat_f32_s:
                    case i32_trunc_sat_f32_u:
                    case i32_trunc_sat_f64_s:
                    case i32_trunc_sat_f64_u:
                    case i64_trunc_sat_f32_s:
                    case i64_trunc_sat_f32_u:
                    case i64_trunc_sat_f64_s:
                    case i64_trunc_sat_f64_u:
                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    default:
                        throw WasmFile::InvalidWASMException(std::format("Unknown opcode {:#x} {}", static_cast<uint32_t>(opcode), static_cast<uint32_t>(secondByte)));
                }
                break;
            }
            case multi_byte_fd: {
                MultiByteFD secondByte = static_cast<MultiByteFD>(stream.read_leb<uint32_t>());
                Opcode realOpcode = static_cast<Opcode>(((static_cast<uint32_t>(opcode)) << 8) | static_cast<uint32_t>(secondByte));
                switch (secondByte)
                {
                    using enum MultiByteFD;
                    case v128_load:
                    case v128_load8x8_s:
                    case v128_load8x8_u:
                    case v128_load16x4_s:
                    case v128_load16x4_u:
                    case v128_load32x2_s:
                    case v128_load32x2_u:
                    case v128_load8_splat:
                    case v128_load16_splat:
                    case v128_load32_splat:
                    case v128_load64_splat:
                    case v128_store:
                    case v128_load32_zero:
                    case v128_load64_zero:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_typed<WasmFile::MemArg>() });
                        break;
                    case v128_const:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_little_endian<uint128_t>() });
                        break;
                    case i8x16_extract_lane_s:
                    case i8x16_extract_lane_u:
                    case i8x16_replace_lane:
                    case i16x8_extract_lane_s:
                    case i16x8_extract_lane_u:
                    case i16x8_replace_lane:
                    case i32x4_extract_lane:
                    case i32x4_replace_lane:
                    case i64x2_extract_lane:
                    case i64x2_replace_lane:
                    case f32x4_extract_lane:
                    case f32x4_replace_lane:
                    case f64x2_extract_lane:
                    case f64x2_replace_lane:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_little_endian<uint8_t>() });
                        break;
                    case v128_load8_lane:
                    case v128_load16_lane:
                    case v128_load32_lane:
                    case v128_load64_lane:
                    case v128_store8_lane:
                    case v128_store16_lane:
                    case v128_store32_lane:
                    case v128_store64_lane:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = LoadStoreLaneArguments { .memArg = stream.read_typed<WasmFile::MemArg>(), .lane = stream.read_little_endian<uint8_t>() } });
                        break;
                    case i8x16_shuffle: {
                        uint8x16_t lanes;
                        for (uint32_t i = 0; i < 16; i++)
                            lanes[i] = stream.read_little_endian<uint8_t>();
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = lanes });
                        break;
                    }
                    case i8x16_swizzle:
                    case i8x16_splat:
                    case i16x8_splat:
                    case i32x4_splat:
                    case i64x2_splat:
                    case f32x4_splat:
                    case f64x2_splat:
                    case i8x16_eq:
                    case i8x16_ne:
                    case i8x16_lt_s:
                    case i8x16_lt_u:
                    case i8x16_gt_s:
                    case i8x16_gt_u:
                    case i8x16_le_s:
                    case i8x16_le_u:
                    case i8x16_ge_s:
                    case i8x16_ge_u:
                    case i16x8_eq:
                    case i16x8_ne:
                    case i16x8_lt_s:
                    case i16x8_lt_u:
                    case i16x8_gt_s:
                    case i16x8_gt_u:
                    case i16x8_le_s:
                    case i16x8_le_u:
                    case i16x8_ge_s:
                    case i16x8_ge_u:
                    case i32x4_eq:
                    case i32x4_ne:
                    case i32x4_lt_s:
                    case i32x4_lt_u:
                    case i32x4_gt_s:
                    case i32x4_gt_u:
                    case i32x4_le_s:
                    case i32x4_le_u:
                    case i32x4_ge_s:
                    case i32x4_ge_u:
                    case f32x4_eq:
                    case f32x4_ne:
                    case f32x4_lt:
                    case f32x4_gt:
                    case f32x4_le:
                    case f32x4_ge:
                    case f64x2_eq:
                    case f64x2_ne:
                    case f64x2_lt:
                    case f64x2_gt:
                    case f64x2_le:
                    case f64x2_ge:
                    case v128_not:
                    case v128_and:
                    case v128_andnot:
                    case v128_or:
                    case v128_xor:
                    case v128_bitselect:
                    case v128_any_true:
                    case f32x4_demote_f64x2_zero:
                    case f64x2_promote_low_f32x4:
                    case i8x16_abs:
                    case i8x16_neg:
                    case i8x16_popcnt:
                    case i8x16_all_true:
                    case i8x16_bitmask:
                    case i8x16_narrow_i16x8_s:
                    case i8x16_narrow_i16x8_u:
                    case f32x4_ceil:
                    case f32x4_floor:
                    case f32x4_trunc:
                    case f32x4_nearest:
                    case i8x16_shl:
                    case i8x16_shr_s:
                    case i8x16_shr_u:
                    case i8x16_add:
                    case i8x16_add_sat_s:
                    case i8x16_add_sat_u:
                    case i8x16_sub:
                    case i8x16_sub_sat_s:
                    case i8x16_sub_sat_u:
                    case f64x2_ceil:
                    case f64x2_floor:
                    case i8x16_min_s:
                    case i8x16_min_u:
                    case i8x16_max_s:
                    case i8x16_max_u:
                    case f64x2_trunc:
                    case i8x16_avgr_u:
                    case i16x8_extadd_pairwise_i8x16_s:
                    case i16x8_extadd_pairwise_i8x16_u:
                    case i32x4_extadd_pairwise_i16x8_s:
                    case i32x4_extadd_pairwise_i16x8_u:
                    case i16x8_abs:
                    case i16x8_neg:
                    case i16x8_q15mulr_sat_s:
                    case i16x8_all_true:
                    case i16x8_bitmask:
                    case i16x8_narrow_i32x4_s:
                    case i16x8_narrow_i32x4_u:
                    case i16x8_extend_low_i8x16_s:
                    case i16x8_extend_high_i8x16_s:
                    case i16x8_extend_low_i8x16_u:
                    case i16x8_extend_high_i8x16_u:
                    case i16x8_shl:
                    case i16x8_shr_s:
                    case i16x8_shr_u:
                    case i16x8_add:
                    case i16x8_add_sat_s:
                    case i16x8_add_sat_u:
                    case i16x8_sub:
                    case i16x8_sub_sat_s:
                    case i16x8_sub_sat_u:
                    case f64x2_nearest:
                    case i16x8_mul:
                    case i16x8_min_s:
                    case i16x8_min_u:
                    case i16x8_max_s:
                    case i16x8_max_u:
                    case i16x8_avgr_u:
                    case i16x8_extmul_low_i8x16_s:
                    case i16x8_extmul_high_i8x16_s:
                    case i16x8_extmul_low_i8x16_u:
                    case i16x8_extmul_high_i8x16_u:
                    case i32x4_abs:
                    case i32x4_neg:
                    case i32x4_all_true:
                    case i32x4_bitmask:
                    case i32x4_extend_low_i16x8_s:
                    case i32x4_extend_high_i16x8_s:
                    case i32x4_extend_low_i16x8_u:
                    case i32x4_extend_high_i16x8_u:
                    case i32x4_shl:
                    case i32x4_shr_s:
                    case i32x4_shr_u:
                    case i32x4_add:
                    case i32x4_sub:
                    case i32x4_mul:
                    case i32x4_min_s:
                    case i32x4_min_u:
                    case i32x4_max_s:
                    case i32x4_max_u:
                    case i32x4_dot_i16x8_s:
                    case i32x4_extmul_low_i16x8_s:
                    case i32x4_extmul_high_i16x8_s:
                    case i32x4_extmul_low_i16x8_u:
                    case i32x4_extmul_high_i16x8_u:
                    case i64x2_abs:
                    case i64x2_neg:
                    case i64x2_all_true:
                    case i64x2_bitmask:
                    case i64x2_extend_low_i32x4_s:
                    case i64x2_extend_high_i32x4_s:
                    case i64x2_extend_low_i32x4_u:
                    case i64x2_extend_high_i32x4_u:
                    case i64x2_shl:
                    case i64x2_shr_s:
                    case i64x2_shr_u:
                    case i64x2_add:
                    case i64x2_sub:
                    case i64x2_mul:
                    case i64x2_eq:
                    case i64x2_ne:
                    case i64x2_lt_s:
                    case i64x2_gt_s:
                    case i64x2_le_s:
                    case i64x2_ge_s:
                    case i64x2_extmul_low_i32x4_s:
                    case i64x2_extmul_high_i32x4_s:
                    case i64x2_extmul_low_i32x4_u:
                    case i64x2_extmul_high_i32x4_u:
                    case f32x4_abs:
                    case f32x4_neg:
                    case f32x4_sqrt:
                    case f32x4_add:
                    case f32x4_sub:
                    case f32x4_mul:
                    case f32x4_div:
                    case f32x4_min:
                    case f32x4_max:
                    case f32x4_pmin:
                    case f32x4_pmax:
                    case f64x2_abs:
                    case f64x2_neg:
                    case f64x2_sqrt:
                    case f64x2_add:
                    case f64x2_sub:
                    case f64x2_mul:
                    case f64x2_div:
                    case f64x2_min:
                    case f64x2_max:
                    case f64x2_pmin:
                    case f64x2_pmax:
                    case i32x4_trunc_sat_f32x4_s:
                    case i32x4_trunc_sat_f32x4_u:
                    case f32x4_convert_i32x4_s:
                    case f32x4_convert_i32x4_u:
                    case i32x4_trunc_sat_f64x2_s_zero:
                    case i32x4_trunc_sat_f64x2_u_zero:
                    case f64x2_convert_low_i32x4_s:
                    case f64x2_convert_low_i32x4_u:
                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    default:
                        throw WasmFile::InvalidWASMException(std::format("Unknown opcode {:#x} {}", static_cast<uint32_t>(opcode), static_cast<uint32_t>(secondByte)));
                }
                break;
            }
            case unreachable:
            case nop:
            case return_:
            case drop:
            case select_:
            case i32_eqz:
            case i32_eq:
            case i32_ne:
            case i32_lt_s:
            case i32_lt_u:
            case i32_gt_s:
            case i32_gt_u:
            case i32_le_s:
            case i32_le_u:
            case i32_ge_s:
            case i32_ge_u:
            case i64_eqz:
            case i64_eq:
            case i64_ne:
            case i64_lt_s:
            case i64_lt_u:
            case i64_gt_s:
            case i64_gt_u:
            case i64_le_s:
            case i64_le_u:
            case i64_ge_s:
            case i64_ge_u:
            case f32_eq:
            case f32_ne:
            case f32_lt:
            case f32_gt:
            case f32_le:
            case f32_ge:
            case f64_eq:
            case f64_ne:
            case f64_lt:
            case f64_gt:
            case f64_le:
            case f64_ge:
            case i32_clz:
            case i32_ctz:
            case i32_popcnt:
            case i32_add:
            case i32_sub:
            case i32_mul:
            case i32_div_s:
            case i32_div_u:
            case i32_rem_s:
            case i32_rem_u:
            case i32_and:
            case i32_or:
            case i32_xor:
            case i32_shl:
            case i32_shr_s:
            case i32_shr_u:
            case i32_rotl:
            case i32_rotr:
            case i64_clz:
            case i64_ctz:
            case i64_popcnt:
            case i64_add:
            case i64_sub:
            case i64_mul:
            case i64_div_s:
            case i64_div_u:
            case i64_rem_s:
            case i64_rem_u:
            case i64_and:
            case i64_or:
            case i64_xor:
            case i64_shl:
            case i64_shr_s:
            case i64_shr_u:
            case i64_rotl:
            case i64_rotr:
            case f32_abs:
            case f32_neg:
            case f32_ceil:
            case f32_floor:
            case f32_trunc:
            case f32_nearest:
            case f32_sqrt:
            case f32_add:
            case f32_sub:
            case f32_mul:
            case f32_div:
            case f32_min:
            case f32_max:
            case f32_copysign:
            case f64_abs:
            case f64_neg:
            case f64_ceil:
            case f64_floor:
            case f64_trunc:
            case f64_nearest:
            case f64_sqrt:
            case f64_add:
            case f64_sub:
            case f64_mul:
            case f64_div:
            case f64_min:
            case f64_max:
            case f64_copysign:
            case i32_wrap_i64:
            case i32_trunc_f32_s:
            case i32_trunc_f32_u:
            case i32_trunc_f64_s:
            case i32_trunc_f64_u:
            case i64_extend_i32_s:
            case i64_extend_i32_u:
            case i64_trunc_f32_s:
            case i64_trunc_f32_u:
            case i64_trunc_f64_s:
            case i64_trunc_f64_u:
            case f32_convert_i32_s:
            case f32_convert_i32_u:
            case f32_convert_i64_s:
            case f32_convert_i64_u:
            case f32_demote_f64:
            case f64_convert_i32_s:
            case f64_convert_i32_u:
            case f64_convert_i64_s:
            case f64_convert_i64_u:
            case f64_promote_f32:
            case i32_reinterpret_f32:
            case i64_reinterpret_f64:
            case f32_reinterpret_i32:
            case f64_reinterpret_i64:
            case i32_extend8_s:
            case i32_extend16_s:
            case i64_extend8_s:
            case i64_extend16_s:
            case i64_extend32_s:
            case ref_is_null:
                instructions.push_back(Instruction { .opcode = opcode });
                break;
            default:
                throw WasmFile::InvalidWASMException(std::format("Error: Unknown opcode {:#x}", static_cast<uint32_t>(opcode)));
        }
    }

    // We were supposed to break out of the loop on the last `end` instruction
    throw WasmFile::InvalidWASMException("Invalid code");
}
