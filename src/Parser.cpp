#include <Parser.h>
#include <Value.h>
#include <cassert>
#include <stack>

std::vector<Instruction> parse(Stream& stream, const WasmFile& wasmFile)
{
    std::vector<Instruction> instructions;
    std::stack<std::tuple<uint32_t, uint32_t, LabelBeginType>> blockBeginStack;

    while (!stream.eof())
    {
        Opcode opcode = (Opcode)stream.read_little_endian<uint8_t>();
        
        switch (opcode)
        {
            case Opcode::block: {
                BlockType blockType = stream.read_typed<BlockType>();
                blockBeginStack.push({ instructions.size(), blockType.get_return_types(wasmFile).size(), LabelBeginType::Other });
                
                instructions.push_back(Instruction { .opcode = opcode, .arguments = BlockLoopArguments { .blockType = blockType }});
                break;
            }
            case Opcode::loop: {
                BlockType blockType = stream.read_typed<BlockType>();
                blockBeginStack.push({ 0, 0, LabelBeginType::LoopInvalid });

                Label label = {
                    .continuation = (uint32_t)instructions.size(),
                    .arity = (uint32_t)blockType.get_param_types(wasmFile).size(),
                    .beginType = LabelBeginType::Other
                };

                instructions.push_back(Instruction { .opcode = opcode, .arguments = BlockLoopArguments { .blockType = blockType, .label = label }});
                break;
            }
            case Opcode::if_: {
                BlockType blockType = stream.read_typed<BlockType>();
                blockBeginStack.push({ instructions.size(), blockType.get_return_types(wasmFile).size(), LabelBeginType::Other });

                instructions.push_back(Instruction { .opcode = opcode, .arguments = IfArguments { .blockType = blockType }});
                break;
            }
            case Opcode::else_: {
                auto begin = blockBeginStack.top();

                assert(std::holds_alternative<IfArguments>(instructions[std::get<0>(begin)].arguments));
                std::get<IfArguments>(instructions[std::get<0>(begin)].arguments).elseLocation = instructions.size();

                instructions.push_back(Instruction { .opcode = opcode });
                break;
            }
            case Opcode::end: {
                instructions.push_back(Instruction { .opcode = opcode });

                if (blockBeginStack.empty())
                    return instructions;

                auto begin = blockBeginStack.top();
                blockBeginStack.pop();

                if (std::get<2>(begin) == LabelBeginType::LoopInvalid)
                    break;

                Label label = { 
                    .continuation = (uint32_t)instructions.size(),
                    .arity = std::get<1>(begin),
                    .beginType = std::get<2>(begin)
                };

                if (std::holds_alternative<BlockLoopArguments>(instructions[std::get<0>(begin)].arguments))
                    std::get<BlockLoopArguments>(instructions[std::get<0>(begin)].arguments).label = label;
                else if (std::holds_alternative<IfArguments>(instructions[std::get<0>(begin)].arguments))
                {
                    IfArguments& arguments = std::get<IfArguments>(instructions[std::get<0>(begin)].arguments);
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
            case Opcode::ref_func:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_leb<uint32_t>() });
                break;
            case Opcode::br_table:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = BranchTableArguments {
                    .labels = stream.read_vec<uint32_t>(),
                    .defaultLabel = stream.read_leb<uint32_t>()
                }});
                break;
            case Opcode::call_indirect:
                instructions.push_back(Instruction { .opcode = opcode, .arguments = CallIndirectArguments {
                    .typeIndex = stream.read_leb<uint32_t>(),
                    .tableIndex = stream.read_leb<uint32_t>(),
                }});
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
                instructions.push_back(Instruction { .opcode = opcode, .arguments = stream.read_typed<MemArg>() });
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
                    throw InvalidWASMException();
                instructions.push_back(Instruction { .opcode = opcode, .arguments = type });
                break;
            }
            case Opcode::multi_byte1: {
                MultiByte1 secondByte = (MultiByte1)stream.read_leb<uint32_t>();
                Opcode realOpcode = (Opcode)((opcode << 8) | secondByte);
                switch (secondByte)
                {
                    case MultiByte1::fc_memory_init: {
                        uint32_t data = stream.read_leb<uint32_t>();
                        if (stream.read_little_endian<uint8_t>() != 0)
                            throw InvalidWASMException();

                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = data });
                        break;
                    }
                    case MultiByte1::fc_memory_copy:
                        if (stream.read_little_endian<uint8_t>() != 0)
                            throw InvalidWASMException();
                        if (stream.read_little_endian<uint8_t>() != 0)
                            throw InvalidWASMException();

                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    case MultiByte1::fc_memory_fill:
                        if (stream.read_little_endian<uint8_t>() != 0)
                            throw InvalidWASMException();

                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    case MultiByte1::fc_table_init:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableInitArguments { .elementIndex = stream.read_leb<uint32_t>(), .tableIndex = stream.read_leb<uint32_t>() }});
                        break;
                    case MultiByte1::fc_table_copy:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = TableCopyArguments { .destination = stream.read_leb<uint32_t>(), .source = stream.read_leb<uint32_t>() }});
                        break;
                    case MultiByte1::fc_data_drop:
                    case MultiByte1::fc_elem_drop:
                    case MultiByte1::fc_table_grow:
                    case MultiByte1::fc_table_size:
                    case MultiByte1::fc_table_fill:
                        instructions.push_back(Instruction { .opcode = realOpcode, .arguments = stream.read_leb<uint32_t>() });
                        break;
                    case MultiByte1::fc_i32_trunc_sat_f32_s:
                    case MultiByte1::fc_i32_trunc_sat_f32_u:
                    case MultiByte1::fc_i32_trunc_sat_f64_s:
                    case MultiByte1::fc_i32_trunc_sat_f64_u:
                    case MultiByte1::fc_i64_trunc_sat_f32_s:
                    case MultiByte1::fc_i64_trunc_sat_f32_u:
                    case MultiByte1::fc_i64_trunc_sat_f64_s:
                    case MultiByte1::fc_i64_trunc_sat_f64_u:
                        instructions.push_back(Instruction { .opcode = realOpcode });
                        break;
                    default:
                        fprintf(stderr, "Error: Unknown opcode 0x%x %d\n", opcode, secondByte);
                        throw InvalidWASMException();
                }
                break;
            }
            case Opcode::memory_size:
            case Opcode::memory_grow:
                if (stream.read_little_endian<uint8_t>() != 0)
                    throw InvalidWASMException();
                [[fallthrough]];
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
                fprintf(stderr, "Error: Unknown opcode 0x%x\n", opcode);
                throw InvalidWASMException();
        }
    }

    // We were supposed to break out of the loop on the last `end` instruction
    throw InvalidWASMException();
}
