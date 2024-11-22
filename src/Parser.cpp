#include <Parser.h>
#include <Value.h>
#include <cassert>
#include <stack>

struct BlockBeginInfo
{
    uint32_t begin;
    uint32_t arity;
    bool isFake;
};

std::vector<Instruction> parse(Stream& stream, Ref<WasmFile::WasmFile> wasmFile)
{
    std::vector<Instruction> instructions;
    std::stack<BlockBeginInfo> blockBeginStack;

    while (!stream.eof())
    {
        Opcode opcode = (Opcode)stream.read_little_endian<uint8_t>();

        switch (opcode)
        {
            case Opcode::block: {
                WasmFile::BlockType blockType = stream.read_typed<WasmFile::BlockType>();
                blockBeginStack.push(BlockBeginInfo {
                    .begin = static_cast<uint32_t>(instructions.size()),
                    .arity = static_cast<uint32_t>(blockType.get_return_types(wasmFile).size()),
                    .isFake = false });

                instructions.push_back(Instruction { .opcode = opcode, .arguments = BlockLoopArguments { .blockType = blockType } });
                break;
            }
            case Opcode::loop: {
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
            case Opcode::if_: {
                WasmFile::BlockType blockType = stream.read_typed<WasmFile::BlockType>();
                blockBeginStack.push(BlockBeginInfo {
                    .begin = static_cast<uint32_t>(instructions.size()),
                    .arity = static_cast<uint32_t>(blockType.get_return_types(wasmFile).size()),
                    .isFake = false });

                instructions.push_back(Instruction { .opcode = opcode, .arguments = IfArguments { .blockType = blockType } });
                break;
            }
            case Opcode::else_: {
                if (blockBeginStack.empty())
                    throw WasmFile::InvalidWASMException();

                auto begin = blockBeginStack.top();
                auto& beginInstruction = instructions[begin.begin];

                assert(std::holds_alternative<IfArguments>(beginInstruction.arguments));
                std::get<IfArguments>(beginInstruction.arguments).elseLocation = instructions.size();

                instructions.push_back(Instruction { .opcode = opcode });
                break;
            }
            case Opcode::end: {
                instructions.push_back(Instruction { .opcode = opcode });

                if (blockBeginStack.empty())
                    return instructions;

                auto begin = blockBeginStack.top();
                blockBeginStack.pop();
                auto& beginInstruction = instructions[begin.begin];

                if (begin.isFake)
                    break;

                Label label = {
                    .continuation = static_cast<uint32_t>(instructions.size()),
                    .arity = begin.arity,
                };

                if (std::holds_alternative<BlockLoopArguments>(beginInstruction.arguments))
                    std::get<BlockLoopArguments>(beginInstruction.arguments).label = label;
                else if (std::holds_alternative<IfArguments>(beginInstruction.arguments))
                {
                    IfArguments& arguments = std::get<IfArguments>(beginInstruction.arguments);
                    arguments.endLabel = label;
                    if (arguments.elseLocation.has_value())
                        instructions[arguments.elseLocation.value()].arguments = label;
                }
                else
                    assert(false);

                break;
            }
            case Opcode::br:
            case Opcode::br_if:
            case Opcode::call:
            case Opcode::local_get:
            case Opcode::local_set:
            case Opcode::local_tee:
            case Opcode::global_get:
            case Opcode::global_set:
            case Opcode::table_get:
            case Opcode::table_set:
            case Opcode::memory_size:
            case Opcode::memory_grow:
            case Opcode::ref_func:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_leb<uint32_t>() });
                break;
            case Opcode::br_table:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = BranchTableArguments { .labels = stream.read_vec<uint32_t>(), .defaultLabel = stream.read_leb<uint32_t>() } });
                break;
            case Opcode::call_indirect:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = CallIndirectArguments {
                                                                           .typeIndex = stream.read_leb<uint32_t>(),
                                                                           .tableIndex = stream.read_leb<uint32_t>(),
                                                                       } });
                break;
            case Opcode::select_typed:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_vec<uint8_t>() });
                break;
            case Opcode::i32_load:
            case Opcode::i64_load:
            case Opcode::f32_load:
            case Opcode::f64_load:
            case Opcode::i32_load8_s:
            case Opcode::i32_load8_u:
            case Opcode::i32_load16_s:
            case Opcode::i32_load16_u:
            case Opcode::i64_load8_s:
            case Opcode::i64_load8_u:
            case Opcode::i64_load16_s:
            case Opcode::i64_load16_u:
            case Opcode::i64_load32_s:
            case Opcode::i64_load32_u:
            case Opcode::i32_store:
            case Opcode::i64_store:
            case Opcode::f32_store:
            case Opcode::f64_store:
            case Opcode::i32_store8:
            case Opcode::i32_store16:
            case Opcode::i64_store8:
            case Opcode::i64_store16:
            case Opcode::i64_store32:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_typed<WasmFile::MemArg>() });
                break;
            case Opcode::i32_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = (uint32_t)stream.read_leb<int32_t, 32>() });
                break;
            case Opcode::i64_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = (uint64_t)stream.read_leb<int64_t, 64>() });
                break;
            case Opcode::f32_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_little_endian<float>() });
                break;
            case Opcode::f64_const:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_little_endian<double>() });
                break;
            case Opcode::ref_null: {
                Type type = (Type)stream.read_little_endian<uint8_t>();
                if (type != Type::funcref && type != Type::externref)
                    throw WasmFile::InvalidWASMException();
                instructions.push_back(Instruction { .opcode = opcode, .arguments = type });
                break;
            }
            case Opcode::multi_byte_fc: {
                MultiByteFC secondByte = (MultiByteFC)stream.read_leb<uint32_t>();
                Opcode realOpcode = (Opcode)((((uint32_t)opcode) << 8) | ((uint32_t)secondByte));
                switch (secondByte)
                {
                    case MultiByteFC::memory_init:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = MemoryInitArguments { .dataIndex = stream.read_leb<uint32_t>(), .memoryIndex = stream.read_little_endian<uint8_t>() } });
                        break;
                    case MultiByteFC::memory_copy:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = MemoryCopyArguments { .source = stream.read_little_endian<uint8_t>(), .destination = stream.read_little_endian<uint8_t>() } });
                        break;
                    case MultiByteFC::table_init:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableInitArguments { .elementIndex = stream.read_leb<uint32_t>(), .tableIndex = stream.read_leb<uint32_t>() } });
                        break;
                    case MultiByteFC::table_copy:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableCopyArguments { .destination = stream.read_leb<uint32_t>(), .source = stream.read_leb<uint32_t>() } });
                        break;
                    case MultiByteFC::data_drop:
                    case MultiByteFC::memory_fill:
                    case MultiByteFC::elem_drop:
                    case MultiByteFC::table_grow:
                    case MultiByteFC::table_size:
                    case MultiByteFC::table_fill:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_leb<uint32_t>() });
                        break;
                    case MultiByteFC::i32_trunc_sat_f32_s:
                    case MultiByteFC::i32_trunc_sat_f32_u:
                    case MultiByteFC::i32_trunc_sat_f64_s:
                    case MultiByteFC::i32_trunc_sat_f64_u:
                    case MultiByteFC::i64_trunc_sat_f32_s:
                    case MultiByteFC::i64_trunc_sat_f32_u:
                    case MultiByteFC::i64_trunc_sat_f64_s:
                    case MultiByteFC::i64_trunc_sat_f64_u:
                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    default:
                        fprintf(stderr, "Error: Unknown opcode 0x%x %d\n", static_cast<uint32_t>(opcode), static_cast<uint32_t>(secondByte));
                        throw WasmFile::InvalidWASMException();
                }
                break;
            }
            case Opcode::multi_byte_fd: {
                MultiByteFD secondByte = (MultiByteFD)stream.read_leb<uint32_t>();
                Opcode realOpcode = (Opcode)((((uint32_t)opcode) << 8) | ((uint32_t)secondByte));
                switch (secondByte)
                {
                    case MultiByteFD::v128_load:
                    case MultiByteFD::v128_load8x8_s:
                    case MultiByteFD::v128_load8x8_u:
                    case MultiByteFD::v128_load16x4_s:
                    case MultiByteFD::v128_load16x4_u:
                    case MultiByteFD::v128_load32x2_s:
                    case MultiByteFD::v128_load32x2_u:
                    case MultiByteFD::v128_load8_splat:
                    case MultiByteFD::v128_load16_splat:
                    case MultiByteFD::v128_load32_splat:
                    case MultiByteFD::v128_load64_splat:
                    case MultiByteFD::v128_store:
                    case MultiByteFD::v128_load32_zero:
                    case MultiByteFD::v128_load64_zero:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_typed<WasmFile::MemArg>() });
                        break;
                    case MultiByteFD::v128_const:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_little_endian<uint128_t>() });
                        break;
                    case MultiByteFD::i8x16_extract_lane_s:
                    case MultiByteFD::i8x16_extract_lane_u:
                    case MultiByteFD::i8x16_replace_lane:
                    case MultiByteFD::i16x8_extract_lane_s:
                    case MultiByteFD::i16x8_extract_lane_u:
                    case MultiByteFD::i16x8_replace_lane:
                    case MultiByteFD::i32x4_extract_lane:
                    case MultiByteFD::i32x4_replace_lane:
                    case MultiByteFD::i64x2_extract_lane:
                    case MultiByteFD::i64x2_replace_lane:
                    case MultiByteFD::f32x4_extract_lane:
                    case MultiByteFD::f32x4_replace_lane:
                    case MultiByteFD::f64x2_extract_lane:
                    case MultiByteFD::f64x2_replace_lane:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_little_endian<uint8_t>() });
                        break;
                    case MultiByteFD::v128_load8_lane:
                    case MultiByteFD::v128_load16_lane:
                    case MultiByteFD::v128_load32_lane:
                    case MultiByteFD::v128_load64_lane:
                    case MultiByteFD::v128_store8_lane:
                    case MultiByteFD::v128_store16_lane:
                    case MultiByteFD::v128_store32_lane:
                    case MultiByteFD::v128_store64_lane:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = LoadStoreLaneArguments { .memArg = stream.read_typed<WasmFile::MemArg>(), .lane = stream.read_little_endian<uint8_t>() } });
                        break;
                    case MultiByteFD::i8x16_shuffle: {
                        uint8x16_t lanes;
                        for (uint32_t i = 0; i < 16; i++)
                            lanes[i] = stream.read_little_endian<uint8_t>();
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = lanes });
                        break;
                    }
                    case MultiByteFD::i8x16_swizzle:
                    case MultiByteFD::i8x16_splat:
                    case MultiByteFD::i16x8_splat:
                    case MultiByteFD::i32x4_splat:
                    case MultiByteFD::i64x2_splat:
                    case MultiByteFD::f32x4_splat:
                    case MultiByteFD::f64x2_splat:
                    case MultiByteFD::i8x16_eq:
                    case MultiByteFD::i8x16_ne:
                    case MultiByteFD::i8x16_lt_s:
                    case MultiByteFD::i8x16_lt_u:
                    case MultiByteFD::i8x16_gt_s:
                    case MultiByteFD::i8x16_gt_u:
                    case MultiByteFD::i8x16_le_s:
                    case MultiByteFD::i8x16_le_u:
                    case MultiByteFD::i8x16_ge_s:
                    case MultiByteFD::i8x16_ge_u:
                    case MultiByteFD::i16x8_eq:
                    case MultiByteFD::i16x8_ne:
                    case MultiByteFD::i16x8_lt_s:
                    case MultiByteFD::i16x8_lt_u:
                    case MultiByteFD::i16x8_gt_s:
                    case MultiByteFD::i16x8_gt_u:
                    case MultiByteFD::i16x8_le_s:
                    case MultiByteFD::i16x8_le_u:
                    case MultiByteFD::i16x8_ge_s:
                    case MultiByteFD::i16x8_ge_u:
                    case MultiByteFD::i32x4_eq:
                    case MultiByteFD::i32x4_ne:
                    case MultiByteFD::i32x4_lt_s:
                    case MultiByteFD::i32x4_lt_u:
                    case MultiByteFD::i32x4_gt_s:
                    case MultiByteFD::i32x4_gt_u:
                    case MultiByteFD::i32x4_le_s:
                    case MultiByteFD::i32x4_le_u:
                    case MultiByteFD::i32x4_ge_s:
                    case MultiByteFD::i32x4_ge_u:
                    case MultiByteFD::f32x4_eq:
                    case MultiByteFD::f32x4_ne:
                    case MultiByteFD::f32x4_lt:
                    case MultiByteFD::f32x4_gt:
                    case MultiByteFD::f32x4_le:
                    case MultiByteFD::f32x4_ge:
                    case MultiByteFD::f64x2_eq:
                    case MultiByteFD::f64x2_ne:
                    case MultiByteFD::f64x2_lt:
                    case MultiByteFD::f64x2_gt:
                    case MultiByteFD::f64x2_le:
                    case MultiByteFD::f64x2_ge:
                    case MultiByteFD::v128_not:
                    case MultiByteFD::v128_and:
                    case MultiByteFD::v128_andnot:
                    case MultiByteFD::v128_or:
                    case MultiByteFD::v128_xor:
                    case MultiByteFD::v128_bitselect:
                    case MultiByteFD::v128_any_true:
                    case MultiByteFD::f32x4_demote_f64x2_zero:
                    case MultiByteFD::f64x2_promote_low_f32x4:
                    case MultiByteFD::i8x16_abs:
                    case MultiByteFD::i8x16_neg:
                    case MultiByteFD::i8x16_popcnt:
                    case MultiByteFD::i8x16_all_true:
                    case MultiByteFD::i8x16_bitmask:
                    case MultiByteFD::i8x16_narrow_i16x8_s:
                    case MultiByteFD::i8x16_narrow_i16x8_u:
                    case MultiByteFD::f32x4_ceil:
                    case MultiByteFD::f32x4_floor:
                    case MultiByteFD::f32x4_trunc:
                    case MultiByteFD::f32x4_nearest:
                    case MultiByteFD::i8x16_shl:
                    case MultiByteFD::i8x16_shr_s:
                    case MultiByteFD::i8x16_shr_u:
                    case MultiByteFD::i8x16_add:
                    case MultiByteFD::i8x16_add_sat_s:
                    case MultiByteFD::i8x16_add_sat_u:
                    case MultiByteFD::i8x16_sub:
                    case MultiByteFD::i8x16_sub_sat_s:
                    case MultiByteFD::i8x16_sub_sat_u:
                    case MultiByteFD::f64x2_ceil:
                    case MultiByteFD::f64x2_floor:
                    case MultiByteFD::i8x16_min_s:
                    case MultiByteFD::i8x16_min_u:
                    case MultiByteFD::i8x16_max_s:
                    case MultiByteFD::i8x16_max_u:
                    case MultiByteFD::f64x2_trunc:
                    case MultiByteFD::i8x16_avgr_u:
                    case MultiByteFD::i16x8_extadd_pairwise_i8x16_s:
                    case MultiByteFD::i16x8_extadd_pairwise_i8x16_u:
                    case MultiByteFD::i32x4_extadd_pairwise_i16x8_s:
                    case MultiByteFD::i32x4_extadd_pairwise_i16x8_u:
                    case MultiByteFD::i16x8_abs:
                    case MultiByteFD::i16x8_neg:
                    case MultiByteFD::i16x8_q15mulr_sat_s:
                    case MultiByteFD::i16x8_all_true:
                    case MultiByteFD::i16x8_bitmask:
                    case MultiByteFD::i16x8_narrow_i32x4_s:
                    case MultiByteFD::i16x8_narrow_i32x4_u:
                    case MultiByteFD::i16x8_extend_low_i8x16_s:
                    case MultiByteFD::i16x8_extend_high_i8x16_s:
                    case MultiByteFD::i16x8_extend_low_i8x16_u:
                    case MultiByteFD::i16x8_extend_high_i8x16_u:
                    case MultiByteFD::i16x8_shl:
                    case MultiByteFD::i16x8_shr_s:
                    case MultiByteFD::i16x8_shr_u:
                    case MultiByteFD::i16x8_add:
                    case MultiByteFD::i16x8_add_sat_s:
                    case MultiByteFD::i16x8_add_sat_u:
                    case MultiByteFD::i16x8_sub:
                    case MultiByteFD::i16x8_sub_sat_s:
                    case MultiByteFD::i16x8_sub_sat_u:
                    case MultiByteFD::f64x2_nearest:
                    case MultiByteFD::i16x8_mul:
                    case MultiByteFD::i16x8_min_s:
                    case MultiByteFD::i16x8_min_u:
                    case MultiByteFD::i16x8_max_s:
                    case MultiByteFD::i16x8_max_u:
                    case MultiByteFD::i16x8_avgr_u:
                    case MultiByteFD::i16x8_extmul_low_i8x16_s:
                    case MultiByteFD::i16x8_extmul_high_i8x16_s:
                    case MultiByteFD::i16x8_extmul_low_i8x16_u:
                    case MultiByteFD::i16x8_extmul_high_i8x16_u:
                    case MultiByteFD::i32x4_abs:
                    case MultiByteFD::i32x4_neg:
                    case MultiByteFD::i32x4_all_true:
                    case MultiByteFD::i32x4_bitmask:
                    case MultiByteFD::i32x4_extend_low_i16x8_s:
                    case MultiByteFD::i32x4_extend_high_i16x8_s:
                    case MultiByteFD::i32x4_extend_low_i16x8_u:
                    case MultiByteFD::i32x4_extend_high_i16x8_u:
                    case MultiByteFD::i32x4_shl:
                    case MultiByteFD::i32x4_shr_s:
                    case MultiByteFD::i32x4_shr_u:
                    case MultiByteFD::i32x4_add:
                    case MultiByteFD::i32x4_sub:
                    case MultiByteFD::i32x4_mul:
                    case MultiByteFD::i32x4_min_s:
                    case MultiByteFD::i32x4_min_u:
                    case MultiByteFD::i32x4_max_s:
                    case MultiByteFD::i32x4_max_u:
                    case MultiByteFD::i32x4_dot_i16x8_s:
                    case MultiByteFD::i32x4_extmul_low_i16x8_s:
                    case MultiByteFD::i32x4_extmul_high_i16x8_s:
                    case MultiByteFD::i32x4_extmul_low_i16x8_u:
                    case MultiByteFD::i32x4_extmul_high_i16x8_u:
                    case MultiByteFD::i64x2_abs:
                    case MultiByteFD::i64x2_neg:
                    case MultiByteFD::i64x2_all_true:
                    case MultiByteFD::i64x2_bitmask:
                    case MultiByteFD::i64x2_extend_low_i32x4_s:
                    case MultiByteFD::i64x2_extend_high_i32x4_s:
                    case MultiByteFD::i64x2_extend_low_i32x4_u:
                    case MultiByteFD::i64x2_extend_high_i32x4_u:
                    case MultiByteFD::i64x2_shl:
                    case MultiByteFD::i64x2_shr_s:
                    case MultiByteFD::i64x2_shr_u:
                    case MultiByteFD::i64x2_add:
                    case MultiByteFD::i64x2_sub:
                    case MultiByteFD::i64x2_mul:
                    case MultiByteFD::i64x2_eq:
                    case MultiByteFD::i64x2_ne:
                    case MultiByteFD::i64x2_lt_s:
                    case MultiByteFD::i64x2_gt_s:
                    case MultiByteFD::i64x2_le_s:
                    case MultiByteFD::i64x2_ge_s:
                    case MultiByteFD::i64x2_extmul_low_i32x4_s:
                    case MultiByteFD::i64x2_extmul_high_i32x4_s:
                    case MultiByteFD::i64x2_extmul_low_i32x4_u:
                    case MultiByteFD::i64x2_extmul_high_i32x4_u:
                    case MultiByteFD::f32x4_abs:
                    case MultiByteFD::f32x4_neg:
                    case MultiByteFD::f32x4_sqrt:
                    case MultiByteFD::f32x4_add:
                    case MultiByteFD::f32x4_sub:
                    case MultiByteFD::f32x4_mul:
                    case MultiByteFD::f32x4_div:
                    case MultiByteFD::f32x4_min:
                    case MultiByteFD::f32x4_max:
                    case MultiByteFD::f32x4_pmin:
                    case MultiByteFD::f32x4_pmax:
                    case MultiByteFD::f64x2_abs:
                    case MultiByteFD::f64x2_neg:
                    case MultiByteFD::f64x2_sqrt:
                    case MultiByteFD::f64x2_add:
                    case MultiByteFD::f64x2_sub:
                    case MultiByteFD::f64x2_mul:
                    case MultiByteFD::f64x2_div:
                    case MultiByteFD::f64x2_min:
                    case MultiByteFD::f64x2_max:
                    case MultiByteFD::f64x2_pmin:
                    case MultiByteFD::f64x2_pmax:
                    case MultiByteFD::i32x4_trunc_sat_f32x4_s:
                    case MultiByteFD::i32x4_trunc_sat_f32x4_u:
                    case MultiByteFD::f32x4_convert_i32x4_s:
                    case MultiByteFD::f32x4_convert_i32x4_u:
                    case MultiByteFD::i32x4_trunc_sat_f64x2_s_zero:
                    case MultiByteFD::i32x4_trunc_sat_f64x2_u_zero:
                    case MultiByteFD::f64x2_convert_low_i32x4_s:
                    case MultiByteFD::f64x2_convert_low_i32x4_u:
                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    default:
                        fprintf(stderr, "Error: Unknown opcode 0x%x %d\n", static_cast<uint32_t>(opcode), static_cast<uint32_t>(secondByte));
                        throw WasmFile::InvalidWASMException();
                }
                break;
            }
            case Opcode::unreachable:
            case Opcode::nop:
            case Opcode::return_:
            case Opcode::drop:
            case Opcode::select_:
            case Opcode::i32_eqz:
            case Opcode::i32_eq:
            case Opcode::i32_ne:
            case Opcode::i32_lt_s:
            case Opcode::i32_lt_u:
            case Opcode::i32_gt_s:
            case Opcode::i32_gt_u:
            case Opcode::i32_le_s:
            case Opcode::i32_le_u:
            case Opcode::i32_ge_s:
            case Opcode::i32_ge_u:
            case Opcode::i64_eqz:
            case Opcode::i64_eq:
            case Opcode::i64_ne:
            case Opcode::i64_lt_s:
            case Opcode::i64_lt_u:
            case Opcode::i64_gt_s:
            case Opcode::i64_gt_u:
            case Opcode::i64_le_s:
            case Opcode::i64_le_u:
            case Opcode::i64_ge_s:
            case Opcode::i64_ge_u:
            case Opcode::f32_eq:
            case Opcode::f32_ne:
            case Opcode::f32_lt:
            case Opcode::f32_gt:
            case Opcode::f32_le:
            case Opcode::f32_ge:
            case Opcode::f64_eq:
            case Opcode::f64_ne:
            case Opcode::f64_lt:
            case Opcode::f64_gt:
            case Opcode::f64_le:
            case Opcode::f64_ge:
            case Opcode::i32_clz:
            case Opcode::i32_ctz:
            case Opcode::i32_popcnt:
            case Opcode::i32_add:
            case Opcode::i32_sub:
            case Opcode::i32_mul:
            case Opcode::i32_div_s:
            case Opcode::i32_div_u:
            case Opcode::i32_rem_s:
            case Opcode::i32_rem_u:
            case Opcode::i32_and:
            case Opcode::i32_or:
            case Opcode::i32_xor:
            case Opcode::i32_shl:
            case Opcode::i32_shr_s:
            case Opcode::i32_shr_u:
            case Opcode::i32_rotl:
            case Opcode::i32_rotr:
            case Opcode::i64_clz:
            case Opcode::i64_ctz:
            case Opcode::i64_popcnt:
            case Opcode::i64_add:
            case Opcode::i64_sub:
            case Opcode::i64_mul:
            case Opcode::i64_div_s:
            case Opcode::i64_div_u:
            case Opcode::i64_rem_s:
            case Opcode::i64_rem_u:
            case Opcode::i64_and:
            case Opcode::i64_or:
            case Opcode::i64_xor:
            case Opcode::i64_shl:
            case Opcode::i64_shr_s:
            case Opcode::i64_shr_u:
            case Opcode::i64_rotl:
            case Opcode::i64_rotr:
            case Opcode::f32_abs:
            case Opcode::f32_neg:
            case Opcode::f32_ceil:
            case Opcode::f32_floor:
            case Opcode::f32_trunc:
            case Opcode::f32_nearest:
            case Opcode::f32_sqrt:
            case Opcode::f32_add:
            case Opcode::f32_sub:
            case Opcode::f32_mul:
            case Opcode::f32_div:
            case Opcode::f32_min:
            case Opcode::f32_max:
            case Opcode::f32_copysign:
            case Opcode::f64_abs:
            case Opcode::f64_neg:
            case Opcode::f64_ceil:
            case Opcode::f64_floor:
            case Opcode::f64_trunc:
            case Opcode::f64_nearest:
            case Opcode::f64_sqrt:
            case Opcode::f64_add:
            case Opcode::f64_sub:
            case Opcode::f64_mul:
            case Opcode::f64_div:
            case Opcode::f64_min:
            case Opcode::f64_max:
            case Opcode::f64_copysign:
            case Opcode::i32_wrap_i64:
            case Opcode::i32_trunc_f32_s:
            case Opcode::i32_trunc_f32_u:
            case Opcode::i32_trunc_f64_s:
            case Opcode::i32_trunc_f64_u:
            case Opcode::i64_extend_i32_s:
            case Opcode::i64_extend_i32_u:
            case Opcode::i64_trunc_f32_s:
            case Opcode::i64_trunc_f32_u:
            case Opcode::i64_trunc_f64_s:
            case Opcode::i64_trunc_f64_u:
            case Opcode::f32_convert_i32_s:
            case Opcode::f32_convert_i32_u:
            case Opcode::f32_convert_i64_s:
            case Opcode::f32_convert_i64_u:
            case Opcode::f32_demote_f64:
            case Opcode::f64_convert_i32_s:
            case Opcode::f64_convert_i32_u:
            case Opcode::f64_convert_i64_s:
            case Opcode::f64_convert_i64_u:
            case Opcode::f64_promote_f32:
            case Opcode::i32_reinterpret_f32:
            case Opcode::i64_reinterpret_f64:
            case Opcode::f32_reinterpret_i32:
            case Opcode::f64_reinterpret_i64:
            case Opcode::i32_extend8_s:
            case Opcode::i32_extend16_s:
            case Opcode::i64_extend8_s:
            case Opcode::i64_extend16_s:
            case Opcode::i64_extend32_s:
            case Opcode::ref_is_null:
                instructions.push_back(Instruction { .opcode = opcode });
                break;
            default:
                fprintf(stderr, "Error: Unknown opcode 0x%x\n", static_cast<uint32_t>(opcode));
                throw WasmFile::InvalidWASMException();
        }
    }

    // We were supposed to break out of the loop on the last `end` instruction
    throw WasmFile::InvalidWASMException();
}
