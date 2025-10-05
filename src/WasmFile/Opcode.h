#pragma once

enum class Opcode
{
    unreachable = 0x00,
    nop = 0x01,
    block = 0x02,
    loop = 0x03,
    if_ = 0x04,
    else_ = 0x05,
    end = 0x0B,
    br = 0x0C,
    br_if = 0x0D,
    br_table = 0x0E,
    return_ = 0x0F,
    call = 0x10,
    call_indirect = 0x11,
    drop = 0x1A,
    select_ = 0x1B,
    select_typed = 0x1C,
    local_get = 0x20,
    local_set = 0x21,
    local_tee = 0x22,
    global_get = 0x23,
    global_set = 0x24,
    table_get = 0x25,
    table_set = 0x26,
    i32_load = 0x28,
    i64_load = 0x29,
    f32_load = 0x2A,
    f64_load = 0x2B,
    i32_load8_s = 0x2C,
    i32_load8_u = 0x2D,
    i32_load16_s = 0x2E,
    i32_load16_u = 0x2F,
    i64_load8_s = 0x30,
    i64_load8_u = 0x31,
    i64_load16_s = 0x32,
    i64_load16_u = 0x33,
    i64_load32_s = 0x34,
    i64_load32_u = 0x35,
    i32_store = 0x36,
    i64_store = 0x37,
    f32_store = 0x38,
    f64_store = 0x39,
    i32_store8 = 0x3A,
    i32_store16 = 0x3B,
    i64_store8 = 0x3C,
    i64_store16 = 0x3D,
    i64_store32 = 0x3E,
    memory_size = 0x3F,
    memory_grow = 0x40,
    i32_const = 0x41,
    i64_const = 0x42,
    f32_const = 0x43,
    f64_const = 0x44,
    i32_eqz = 0x45,
    i32_eq = 0x46,
    i32_ne = 0x47,
    i32_lt_s = 0x48,
    i32_lt_u = 0x49,
    i32_gt_s = 0x4A,
    i32_gt_u = 0x4B,
    i32_le_s = 0x4C,
    i32_le_u = 0x4D,
    i32_ge_s = 0x4E,
    i32_ge_u = 0x4F,
    i64_eqz = 0x50,
    i64_eq = 0x51,
    i64_ne = 0x52,
    i64_lt_s = 0x53,
    i64_lt_u = 0x54,
    i64_gt_s = 0x55,
    i64_gt_u = 0x56,
    i64_le_s = 0x57,
    i64_le_u = 0x58,
    i64_ge_s = 0x59,
    i64_ge_u = 0x5A,
    f32_eq = 0x5B,
    f32_ne = 0x5C,
    f32_lt = 0x5D,
    f32_gt = 0x5E,
    f32_le = 0x5F,
    f32_ge = 0x60,
    f64_eq = 0x61,
    f64_ne = 0x62,
    f64_lt = 0x63,
    f64_gt = 0x64,
    f64_le = 0x65,
    f64_ge = 0x66,
    i32_clz = 0x67,
    i32_ctz = 0x68,
    i32_popcnt = 0x69,
    i32_add = 0x6A,
    i32_sub = 0x6B,
    i32_mul = 0x6C,
    i32_div_s = 0x6D,
    i32_div_u = 0x6E,
    i32_rem_s = 0x6F,
    i32_rem_u = 0x70,
    i32_and = 0x71,
    i32_or = 0x72,
    i32_xor = 0x73,
    i32_shl = 0x74,
    i32_shr_s = 0x75,
    i32_shr_u = 0x76,
    i32_rotl = 0x77,
    i32_rotr = 0x78,
    i64_clz = 0x79,
    i64_ctz = 0x7A,
    i64_popcnt = 0x7B,
    i64_add = 0x7C,
    i64_sub = 0x7D,
    i64_mul = 0x7E,
    i64_div_s = 0x7F,
    i64_div_u = 0x80,
    i64_rem_s = 0x81,
    i64_rem_u = 0x82,
    i64_and = 0x83,
    i64_or = 0x84,
    i64_xor = 0x85,
    i64_shl = 0x86,
    i64_shr_s = 0x87,
    i64_shr_u = 0x88,
    i64_rotl = 0x89,
    i64_rotr = 0x8A,
    f32_abs = 0x8B,
    f32_neg = 0x8C,
    f32_ceil = 0x8D,
    f32_floor = 0x8E,
    f32_trunc = 0x8F,
    f32_nearest = 0x90,
    f32_sqrt = 0x91,
    f32_add = 0x92,
    f32_sub = 0x93,
    f32_mul = 0x94,
    f32_div = 0x95,
    f32_min = 0x96,
    f32_max = 0x97,
    f32_copysign = 0x98,
    f64_abs = 0x99,
    f64_neg = 0x9A,
    f64_ceil = 0x9B,
    f64_floor = 0x9C,
    f64_trunc = 0x9D,
    f64_nearest = 0x9E,
    f64_sqrt = 0x9F,
    f64_add = 0xA0,
    f64_sub = 0xA1,
    f64_mul = 0xA2,
    f64_div = 0xA3,
    f64_min = 0xA4,
    f64_max = 0xA5,
    f64_copysign = 0xA6,
    i32_wrap_i64 = 0xA7,
    i32_trunc_f32_s = 0xA8,
    i32_trunc_f32_u = 0xA9,
    i32_trunc_f64_s = 0xAA,
    i32_trunc_f64_u = 0xAB,
    i64_extend_i32_s = 0xAC,
    i64_extend_i32_u = 0xAD,
    i64_trunc_f32_s = 0xAE,
    i64_trunc_f32_u = 0xAF,
    i64_trunc_f64_s = 0xB0,
    i64_trunc_f64_u = 0xB1,
    f32_convert_i32_s = 0xB2,
    f32_convert_i32_u = 0xB3,
    f32_convert_i64_s = 0xB4,
    f32_convert_i64_u = 0xB5,
    f32_demote_f64 = 0xB6,
    f64_convert_i32_s = 0xB7,
    f64_convert_i32_u = 0xB8,
    f64_convert_i64_s = 0xB9,
    f64_convert_i64_u = 0xBA,
    f64_promote_f32 = 0xBB,
    i32_reinterpret_f32 = 0xBC,
    i64_reinterpret_f64 = 0xBD,
    f32_reinterpret_i32 = 0xBE,
    f64_reinterpret_i64 = 0xBF,
    i32_extend8_s = 0xC0,
    i32_extend16_s = 0xC1,
    i64_extend8_s = 0xC2,
    i64_extend16_s = 0xC3,
    i64_extend32_s = 0xC4,
    ref_null = 0xD0,
    ref_is_null = 0xD1,
    ref_func = 0xD2,
    multi_byte_fc = 0xFC,
    multi_byte_fd = 0xFD,

    i32_trunc_sat_f32_s = 0xFC0000,
    i32_trunc_sat_f32_u = 0xFC0001,
    i32_trunc_sat_f64_s = 0xFC0002,
    i32_trunc_sat_f64_u = 0xFC0003,
    i64_trunc_sat_f32_s = 0xFC0004,
    i64_trunc_sat_f32_u = 0xFC0005,
    i64_trunc_sat_f64_s = 0xFC0006,
    i64_trunc_sat_f64_u = 0xFC0007,
    memory_init = 0xFC0008,
    data_drop = 0xFC0009,
    memory_copy = 0xFC000A,
    memory_fill = 0xFC000B,
    table_init = 0xFC000C,
    elem_drop = 0xFC000D,
    table_copy = 0xFC000E,
    table_grow = 0xFC000F,
    table_size = 0xFC0010,
    table_fill = 0xFC0011,

    v128_load = 0xFD0000,
    v128_load8x8_s = 0xFD0001,
    v128_load8x8_u = 0xFD0002,
    v128_load16x4_s = 0xFD0003,
    v128_load16x4_u = 0xFD0004,
    v128_load32x2_s = 0xFD0005,
    v128_load32x2_u = 0xFD0006,
    v128_load8_splat = 0xFD0007,
    v128_load16_splat = 0xFD0008,
    v128_load32_splat = 0xFD0009,
    v128_load64_splat = 0xFD000A,
    v128_store = 0xFD000B,
    v128_const = 0xFD000C,
    i8x16_shuffle = 0xFD000D,
    i8x16_swizzle = 0xFD000E,
    i8x16_splat = 0xFD000F,
    i16x8_splat = 0xFD0010,
    i32x4_splat = 0xFD0011,
    i64x2_splat = 0xFD0012,
    f32x4_splat = 0xFD0013,
    f64x2_splat = 0xFD0014,
    i8x16_extract_lane_s = 0xFD0015,
    i8x16_extract_lane_u = 0xFD0016,
    i8x16_replace_lane = 0xFD0017,
    i16x8_extract_lane_s = 0xFD0018,
    i16x8_extract_lane_u = 0xFD0019,
    i16x8_replace_lane = 0xFD001A,
    i32x4_extract_lane = 0xFD001B,
    i32x4_replace_lane = 0xFD001C,
    i64x2_extract_lane = 0xFD001D,
    i64x2_replace_lane = 0xFD001E,
    f32x4_extract_lane = 0xFD001F,
    f32x4_replace_lane = 0xFD0020,
    f64x2_extract_lane = 0xFD0021,
    f64x2_replace_lane = 0xFD0022,
    i8x16_eq = 0xFD0023,
    i8x16_ne = 0xFD0024,
    i8x16_lt_s = 0xFD0025,
    i8x16_lt_u = 0xFD0026,
    i8x16_gt_s = 0xFD0027,
    i8x16_gt_u = 0xFD0028,
    i8x16_le_s = 0xFD0029,
    i8x16_le_u = 0xFD002A,
    i8x16_ge_s = 0xFD002B,
    i8x16_ge_u = 0xFD002C,
    i16x8_eq = 0xFD002D,
    i16x8_ne = 0xFD002E,
    i16x8_lt_s = 0xFD002F,
    i16x8_lt_u = 0xFD0030,
    i16x8_gt_s = 0xFD0031,
    i16x8_gt_u = 0xFD0032,
    i16x8_le_s = 0xFD0033,
    i16x8_le_u = 0xFD0034,
    i16x8_ge_s = 0xFD0035,
    i16x8_ge_u = 0xFD0036,
    i32x4_eq = 0xFD0037,
    i32x4_ne = 0xFD0038,
    i32x4_lt_s = 0xFD0039,
    i32x4_lt_u = 0xFD003A,
    i32x4_gt_s = 0xFD003B,
    i32x4_gt_u = 0xFD003C,
    i32x4_le_s = 0xFD003D,
    i32x4_le_u = 0xFD003E,
    i32x4_ge_s = 0xFD003F,
    i32x4_ge_u = 0xFD0040,
    f32x4_eq = 0xFD0041,
    f32x4_ne = 0xFD0042,
    f32x4_lt = 0xFD0043,
    f32x4_gt = 0xFD0044,
    f32x4_le = 0xFD0045,
    f32x4_ge = 0xFD0046,
    f64x2_eq = 0xFD0047,
    f64x2_ne = 0xFD0048,
    f64x2_lt = 0xFD0049,
    f64x2_gt = 0xFD004A,
    f64x2_le = 0xFD004B,
    f64x2_ge = 0xFD004C,
    v128_not = 0xFD004D,
    v128_and = 0xFD004E,
    v128_andnot = 0xFD004F,
    v128_or = 0xFD0050,
    v128_xor = 0xFD0051,
    v128_bitselect = 0xFD0052,
    v128_any_true = 0xFD0053,
    v128_load8_lane = 0xFD0054,
    v128_load16_lane = 0xFD0055,
    v128_load32_lane = 0xFD0056,
    v128_load64_lane = 0xFD0057,
    v128_store8_lane = 0xFD0058,
    v128_store16_lane = 0xFD0059,
    v128_store32_lane = 0xFD005A,
    v128_store64_lane = 0xFD005B,
    v128_load32_zero = 0xFD005C,
    v128_load64_zero = 0xFD005D,
    f32x4_demote_f64x2_zero = 0xFD005E,
    f64x2_promote_low_f32x4 = 0xFD005F,
    i8x16_abs = 0xFD0060,
    i8x16_neg = 0xFD0061,
    i8x16_popcnt = 0xFD0062,
    i8x16_all_true = 0xFD0063,
    i8x16_bitmask = 0xFD0064,
    i8x16_narrow_i16x8_s = 0xFD0065,
    i8x16_narrow_i16x8_u = 0xFD0066,
    f32x4_ceil = 0xFD0067,
    f32x4_floor = 0xFD0068,
    f32x4_trunc = 0xFD0069,
    f32x4_nearest = 0xFD006A,
    i8x16_shl = 0xFD006B,
    i8x16_shr_s = 0xFD006C,
    i8x16_shr_u = 0xFD006D,
    i8x16_add = 0xFD006E,
    i8x16_add_sat_s = 0xFD006F,
    i8x16_add_sat_u = 0xFD0070,
    i8x16_sub = 0xFD0071,
    i8x16_sub_sat_s = 0xFD0072,
    i8x16_sub_sat_u = 0xFD0073,
    f64x2_ceil = 0xFD0074,
    f64x2_floor = 0xFD0075,
    i8x16_min_s = 0xFD0076,
    i8x16_min_u = 0xFD0077,
    i8x16_max_s = 0xFD0078,
    i8x16_max_u = 0xFD0079,
    f64x2_trunc = 0xFD007A,
    i8x16_avgr_u = 0xFD007B,
    i16x8_extadd_pairwise_i8x16_s = 0xFD007C,
    i16x8_extadd_pairwise_i8x16_u = 0xFD007D,
    i32x4_extadd_pairwise_i16x8_s = 0xFD007E,
    i32x4_extadd_pairwise_i16x8_u = 0xFD007F,
    i16x8_abs = 0xFD0080,
    i16x8_neg = 0xFD0081,
    i16x8_q15mulr_sat_s = 0xFD0082,
    i16x8_all_true = 0xFD0083,
    i16x8_bitmask = 0xFD0084,
    i16x8_narrow_i32x4_s = 0xFD0085,
    i16x8_narrow_i32x4_u = 0xFD0086,
    i16x8_extend_low_i8x16_s = 0xFD0087,
    i16x8_extend_high_i8x16_s = 0xFD0088,
    i16x8_extend_low_i8x16_u = 0xFD0089,
    i16x8_extend_high_i8x16_u = 0xFD008A,
    i16x8_shl = 0xFD008B,
    i16x8_shr_s = 0xFD008C,
    i16x8_shr_u = 0xFD008D,
    i16x8_add = 0xFD008E,
    i16x8_add_sat_s = 0xFD008F,
    i16x8_add_sat_u = 0xFD0090,
    i16x8_sub = 0xFD0091,
    i16x8_sub_sat_s = 0xFD0092,
    i16x8_sub_sat_u = 0xFD0093,
    f64x2_nearest = 0xFD0094,
    i16x8_mul = 0xFD0095,
    i16x8_min_s = 0xFD0096,
    i16x8_min_u = 0xFD0097,
    i16x8_max_s = 0xFD0098,
    i16x8_max_u = 0xFD0099,
    i16x8_avgr_u = 0xFD009B,
    i16x8_extmul_low_i8x16_s = 0xFD009C,
    i16x8_extmul_high_i8x16_s = 0xFD009D,
    i16x8_extmul_low_i8x16_u = 0xFD009E,
    i16x8_extmul_high_i8x16_u = 0xFD009F,
    i32x4_abs = 0xFD00A0,
    i32x4_neg = 0xFD00A1,
    i32x4_all_true = 0xFD00A3,
    i32x4_bitmask = 0xFD00A4,
    i32x4_extend_low_i16x8_s = 0xFD00A7,
    i32x4_extend_high_i16x8_s = 0xFD00A8,
    i32x4_extend_low_i16x8_u = 0xFD00A9,
    i32x4_extend_high_i16x8_u = 0xFD00AA,
    i32x4_shl = 0xFD00AB,
    i32x4_shr_s = 0xFD00AC,
    i32x4_shr_u = 0xFD00AD,
    i32x4_add = 0xFD00AE,
    i32x4_sub = 0xFD00B1,
    i32x4_mul = 0xFD00B5,
    i32x4_min_s = 0xFD00B6,
    i32x4_min_u = 0xFD00B7,
    i32x4_max_s = 0xFD00B8,
    i32x4_max_u = 0xFD00B9,
    i32x4_dot_i16x8_s = 0xFD00BA,
    i32x4_extmul_low_i16x8_s = 0xFD00BC,
    i32x4_extmul_high_i16x8_s = 0xFD00BD,
    i32x4_extmul_low_i16x8_u = 0xFD00BE,
    i32x4_extmul_high_i16x8_u = 0xFD00BF,
    i64x2_abs = 0xFD00C0,
    i64x2_neg = 0xFD00C1,
    i64x2_all_true = 0xFD00C3,
    i64x2_bitmask = 0xFD00C4,
    i64x2_extend_low_i32x4_s = 0xFD00C7,
    i64x2_extend_high_i32x4_s = 0xFD00C8,
    i64x2_extend_low_i32x4_u = 0xFD00C9,
    i64x2_extend_high_i32x4_u = 0xFD00CA,
    i64x2_shl = 0xFD00CB,
    i64x2_shr_s = 0xFD00CC,
    i64x2_shr_u = 0xFD00CD,
    i64x2_add = 0xFD00CE,
    i64x2_sub = 0xFD00D1,
    i64x2_mul = 0xFD00D5,
    i64x2_eq = 0xFD00D6,
    i64x2_ne = 0xFD00D7,
    i64x2_lt_s = 0xFD00D8,
    i64x2_gt_s = 0xFD00D9,
    i64x2_le_s = 0xFD00DA,
    i64x2_ge_s = 0xFD00DB,
    i64x2_extmul_low_i32x4_s = 0xFD00DC,
    i64x2_extmul_high_i32x4_s = 0xFD00DD,
    i64x2_extmul_low_i32x4_u = 0xFD00DE,
    i64x2_extmul_high_i32x4_u = 0xFD00DF,
    f32x4_abs = 0xFD00E0,
    f32x4_neg = 0xFD00E1,
    f32x4_sqrt = 0xFD00E3,
    f32x4_add = 0xFD00E4,
    f32x4_sub = 0xFD00E5,
    f32x4_mul = 0xFD00E6,
    f32x4_div = 0xFD00E7,
    f32x4_min = 0xFD00E8,
    f32x4_max = 0xFD00E9,
    f32x4_pmin = 0xFD00EA,
    f32x4_pmax = 0xFD00EB,
    f64x2_abs = 0xFD00EC,
    f64x2_neg = 0xFD00ED,
    f64x2_sqrt = 0xFD00EF,
    f64x2_add = 0xFD00F0,
    f64x2_sub = 0xFD00F1,
    f64x2_mul = 0xFD00F2,
    f64x2_div = 0xFD00F3,
    f64x2_min = 0xFD00F4,
    f64x2_max = 0xFD00F5,
    f64x2_pmin = 0xFD00F6,
    f64x2_pmax = 0xFD00F7,
    i32x4_trunc_sat_f32x4_s = 0xFD00F8,
    i32x4_trunc_sat_f32x4_u = 0xFD00F9,
    f32x4_convert_i32x4_s = 0xFD00FA,
    f32x4_convert_i32x4_u = 0xFD00FB,
    i32x4_trunc_sat_f64x2_s_zero = 0xFD00FC,
    i32x4_trunc_sat_f64x2_u_zero = 0xFD00FD,
    f64x2_convert_low_i32x4_s = 0xFD00FE,
    f64x2_convert_low_i32x4_u = 0xFD00FF,

    i8x16_relaxed_swizzle = 0xFD0100,
    i32x4_relaxed_trunc_f32x4_s = 0xFD0101,
    i32x4_relaxed_trunc_f32x4_u = 0xFD0102,
    i32x4_relaxed_trunc_f64x2_s_zero = 0xFD0103,
    i32x4_relaxed_trunc_f64x2_u_zero = 0xFD0104,
    f32x4_relaxed_madd = 0xFD0105,
    f32x4_relaxed_nmadd = 0xFD0106,
    f64x2_relaxed_madd = 0xFD0107,
    f64x2_relaxed_nmadd = 0xFD0108,
    i8x16_relaxed_laneselect = 0xFD0109,
    i16x8_relaxed_laneselect = 0xFD010A,
    i32x4_relaxed_laneselect = 0xFD010B,
    i64x2_relaxed_laneselect = 0xFD010C,
    f32x4_relaxed_min = 0xFD010D,
    f32x4_relaxed_max = 0xFD010E,
    f64x2_relaxed_min = 0xFD010F,
    f64x2_relaxed_max = 0xFD0110,
    i16x8_relaxed_q15mulr_s = 0xFD0111,
    i16x8_relaxed_dot_i8x16_i7x16_s = 0xFD0112,
    i32x4_relaxed_dot_i8x16_i7x16_add_s = 0xFD0113,
};

enum class MultiByteFC
{
    i32_trunc_sat_f32_s = 0,
    i32_trunc_sat_f32_u = 1,
    i32_trunc_sat_f64_s = 2,
    i32_trunc_sat_f64_u = 3,
    i64_trunc_sat_f32_s = 4,
    i64_trunc_sat_f32_u = 5,
    i64_trunc_sat_f64_s = 6,
    i64_trunc_sat_f64_u = 7,
    memory_init = 8,
    data_drop = 9,
    memory_copy = 10,
    memory_fill = 11,
    table_init = 12,
    elem_drop = 13,
    table_copy = 14,
    table_grow = 15,
    table_size = 16,
    table_fill = 17,
};

enum class MultiByteFD
{
    v128_load = 0,
    v128_load8x8_s = 1,
    v128_load8x8_u = 2,
    v128_load16x4_s = 3,
    v128_load16x4_u = 4,
    v128_load32x2_s = 5,
    v128_load32x2_u = 6,
    v128_load8_splat = 7,
    v128_load16_splat = 8,
    v128_load32_splat = 9,
    v128_load64_splat = 10,
    v128_store = 11,
    v128_const = 12,
    i8x16_shuffle = 13,
    i8x16_swizzle = 14,
    i8x16_splat = 15,
    i16x8_splat = 16,
    i32x4_splat = 17,
    i64x2_splat = 18,
    f32x4_splat = 19,
    f64x2_splat = 20,
    i8x16_extract_lane_s = 21,
    i8x16_extract_lane_u = 22,
    i8x16_replace_lane = 23,
    i16x8_extract_lane_s = 24,
    i16x8_extract_lane_u = 25,
    i16x8_replace_lane = 26,
    i32x4_extract_lane = 27,
    i32x4_replace_lane = 28,
    i64x2_extract_lane = 29,
    i64x2_replace_lane = 30,
    f32x4_extract_lane = 31,
    f32x4_replace_lane = 32,
    f64x2_extract_lane = 33,
    f64x2_replace_lane = 34,
    i8x16_eq = 35,
    i8x16_ne = 36,
    i8x16_lt_s = 37,
    i8x16_lt_u = 38,
    i8x16_gt_s = 39,
    i8x16_gt_u = 40,
    i8x16_le_s = 41,
    i8x16_le_u = 42,
    i8x16_ge_s = 43,
    i8x16_ge_u = 44,
    i16x8_eq = 45,
    i16x8_ne = 46,
    i16x8_lt_s = 47,
    i16x8_lt_u = 48,
    i16x8_gt_s = 49,
    i16x8_gt_u = 50,
    i16x8_le_s = 51,
    i16x8_le_u = 52,
    i16x8_ge_s = 53,
    i16x8_ge_u = 54,
    i32x4_eq = 55,
    i32x4_ne = 56,
    i32x4_lt_s = 57,
    i32x4_lt_u = 58,
    i32x4_gt_s = 59,
    i32x4_gt_u = 60,
    i32x4_le_s = 61,
    i32x4_le_u = 62,
    i32x4_ge_s = 63,
    i32x4_ge_u = 64,
    f32x4_eq = 65,
    f32x4_ne = 66,
    f32x4_lt = 67,
    f32x4_gt = 68,
    f32x4_le = 69,
    f32x4_ge = 70,
    f64x2_eq = 71,
    f64x2_ne = 72,
    f64x2_lt = 73,
    f64x2_gt = 74,
    f64x2_le = 75,
    f64x2_ge = 76,
    v128_not = 77,
    v128_and = 78,
    v128_andnot = 79,
    v128_or = 80,
    v128_xor = 81,
    v128_bitselect = 82,
    v128_any_true = 83,
    v128_load8_lane = 84,
    v128_load16_lane = 85,
    v128_load32_lane = 86,
    v128_load64_lane = 87,
    v128_store8_lane = 88,
    v128_store16_lane = 89,
    v128_store32_lane = 90,
    v128_store64_lane = 91,
    v128_load32_zero = 92,
    v128_load64_zero = 93,
    f32x4_demote_f64x2_zero = 94,
    f64x2_promote_low_f32x4 = 95,
    i8x16_abs = 96,
    i8x16_neg = 97,
    i8x16_popcnt = 98,
    i8x16_all_true = 99,
    i8x16_bitmask = 100,
    i8x16_narrow_i16x8_s = 101,
    i8x16_narrow_i16x8_u = 102,
    f32x4_ceil = 103,
    f32x4_floor = 104,
    f32x4_trunc = 105,
    f32x4_nearest = 106,
    i8x16_shl = 107,
    i8x16_shr_s = 108,
    i8x16_shr_u = 109,
    i8x16_add = 110,
    i8x16_add_sat_s = 111,
    i8x16_add_sat_u = 112,
    i8x16_sub = 113,
    i8x16_sub_sat_s = 114,
    i8x16_sub_sat_u = 115,
    f64x2_ceil = 116,
    f64x2_floor = 117,
    i8x16_min_s = 118,
    i8x16_min_u = 119,
    i8x16_max_s = 120,
    i8x16_max_u = 121,
    f64x2_trunc = 122,
    i8x16_avgr_u = 123,
    i16x8_extadd_pairwise_i8x16_s = 124,
    i16x8_extadd_pairwise_i8x16_u = 125,
    i32x4_extadd_pairwise_i16x8_s = 126,
    i32x4_extadd_pairwise_i16x8_u = 127,
    i16x8_abs = 128,
    i16x8_neg = 129,
    i16x8_q15mulr_sat_s = 130,
    i16x8_all_true = 131,
    i16x8_bitmask = 132,
    i16x8_narrow_i32x4_s = 133,
    i16x8_narrow_i32x4_u = 134,
    i16x8_extend_low_i8x16_s = 135,
    i16x8_extend_high_i8x16_s = 136,
    i16x8_extend_low_i8x16_u = 137,
    i16x8_extend_high_i8x16_u = 138,
    i16x8_shl = 139,
    i16x8_shr_s = 140,
    i16x8_shr_u = 141,
    i16x8_add = 142,
    i16x8_add_sat_s = 143,
    i16x8_add_sat_u = 144,
    i16x8_sub = 145,
    i16x8_sub_sat_s = 146,
    i16x8_sub_sat_u = 147,
    f64x2_nearest = 148,
    i16x8_mul = 149,
    i16x8_min_s = 150,
    i16x8_min_u = 151,
    i16x8_max_s = 152,
    i16x8_max_u = 153,
    i16x8_avgr_u = 155,
    i16x8_extmul_low_i8x16_s = 156,
    i16x8_extmul_high_i8x16_s = 157,
    i16x8_extmul_low_i8x16_u = 158,
    i16x8_extmul_high_i8x16_u = 159,
    i32x4_abs = 160,
    i32x4_neg = 161,
    i32x4_all_true = 163,
    i32x4_bitmask = 164,
    i32x4_extend_low_i16x8_s = 167,
    i32x4_extend_high_i16x8_s = 168,
    i32x4_extend_low_i16x8_u = 169,
    i32x4_extend_high_i16x8_u = 170,
    i32x4_shl = 171,
    i32x4_shr_s = 172,
    i32x4_shr_u = 173,
    i32x4_add = 174,
    i32x4_sub = 177,
    i32x4_mul = 181,
    i32x4_min_s = 182,
    i32x4_min_u = 183,
    i32x4_max_s = 184,
    i32x4_max_u = 185,
    i32x4_dot_i16x8_s = 186,
    i32x4_extmul_low_i16x8_s = 188,
    i32x4_extmul_high_i16x8_s = 189,
    i32x4_extmul_low_i16x8_u = 190,
    i32x4_extmul_high_i16x8_u = 191,
    i64x2_abs = 192,
    i64x2_neg = 193,
    i64x2_all_true = 195,
    i64x2_bitmask = 196,
    i64x2_extend_low_i32x4_s = 199,
    i64x2_extend_high_i32x4_s = 200,
    i64x2_extend_low_i32x4_u = 201,
    i64x2_extend_high_i32x4_u = 202,
    i64x2_shl = 203,
    i64x2_shr_s = 204,
    i64x2_shr_u = 205,
    i64x2_add = 206,
    i64x2_sub = 209,
    i64x2_mul = 213,
    i64x2_eq = 214,
    i64x2_ne = 215,
    i64x2_lt_s = 216,
    i64x2_gt_s = 217,
    i64x2_le_s = 218,
    i64x2_ge_s = 219,
    i64x2_extmul_low_i32x4_s = 220,
    i64x2_extmul_high_i32x4_s = 221,
    i64x2_extmul_low_i32x4_u = 222,
    i64x2_extmul_high_i32x4_u = 223,
    f32x4_abs = 224,
    f32x4_neg = 225,
    f32x4_sqrt = 227,
    f32x4_add = 228,
    f32x4_sub = 229,
    f32x4_mul = 230,
    f32x4_div = 231,
    f32x4_min = 232,
    f32x4_max = 233,
    f32x4_pmin = 234,
    f32x4_pmax = 235,
    f64x2_abs = 236,
    f64x2_neg = 237,
    f64x2_sqrt = 239,
    f64x2_add = 240,
    f64x2_sub = 241,
    f64x2_mul = 242,
    f64x2_div = 243,
    f64x2_min = 244,
    f64x2_max = 245,
    f64x2_pmin = 246,
    f64x2_pmax = 247,
    i32x4_trunc_sat_f32x4_s = 248,
    i32x4_trunc_sat_f32x4_u = 249,
    f32x4_convert_i32x4_s = 250,
    f32x4_convert_i32x4_u = 251,
    i32x4_trunc_sat_f64x2_s_zero = 252,
    i32x4_trunc_sat_f64x2_u_zero = 253,
    f64x2_convert_low_i32x4_s = 254,
    f64x2_convert_low_i32x4_u = 255,

    i8x16_relaxed_swizzle = 256,
    i32x4_relaxed_trunc_f32x4_s = 257,
    i32x4_relaxed_trunc_f32x4_u = 258,
    i32x4_relaxed_trunc_f64x2_s_zero = 259,
    i32x4_relaxed_trunc_f64x2_u_zero = 260,
    f32x4_relaxed_madd = 261,
    f32x4_relaxed_nmadd = 262,
    f64x2_relaxed_madd = 263,
    f64x2_relaxed_nmadd = 264,
    i8x16_relaxed_laneselect = 265,
    i16x8_relaxed_laneselect = 266,
    i32x4_relaxed_laneselect = 267,
    i64x2_relaxed_laneselect = 268,
    f32x4_relaxed_min = 269,
    f32x4_relaxed_max = 270,
    f64x2_relaxed_min = 271,
    f64x2_relaxed_max = 272,
    i16x8_relaxed_q15mulr_s = 273,
    i16x8_relaxed_dot_i8x16_i7x16_s = 274,
    i32x4_relaxed_dot_i8x16_i7x16_add_s = 275,
};

// X(opcode, memoryType, targetType)
#define ENUMERATE_LOAD_OPERATIONS(X)           \
    X(i32_load, uint32_t, uint32_t)            \
    X(i64_load, uint64_t, uint64_t)            \
    X(f32_load, float, float)                  \
    X(f64_load, double, double)                \
    X(i32_load8_s, int8_t, uint32_t)           \
    X(i32_load8_u, uint8_t, uint32_t)          \
    X(i32_load16_s, int16_t, uint32_t)         \
    X(i32_load16_u, uint16_t, uint32_t)        \
    X(i64_load8_s, int8_t, uint64_t)           \
    X(i64_load8_u, uint8_t, uint64_t)          \
    X(i64_load16_s, int16_t, uint64_t)         \
    X(i64_load16_u, uint16_t, uint64_t)        \
    X(i64_load32_s, int32_t, uint64_t)         \
    X(i64_load32_u, uint32_t, uint64_t)        \
    X(v128_load, uint128_t, uint128_t)         \
    X(v128_load8x8_s, int8x8_t, int16x8_t)     \
    X(v128_load8x8_u, uint8x8_t, uint16x8_t)   \
    X(v128_load16x4_s, int16x4_t, int32x4_t)   \
    X(v128_load16x4_u, uint16x4_t, uint32x4_t) \
    X(v128_load32x2_s, int32x2_t, int64x2_t)   \
    X(v128_load32x2_u, uint32x2_t, uint64x2_t)

// X(opcode, memoryType, targetType)
#define ENUMERATE_STORE_OPERATIONS(X)  \
    X(i32_store, uint32_t, uint32_t)   \
    X(i64_store, uint64_t, uint64_t)   \
    X(f32_store, float, float)         \
    X(f64_store, double, double)       \
    X(i32_store8, uint8_t, uint32_t)   \
    X(i32_store16, uint16_t, uint32_t) \
    X(i64_store8, uint8_t, uint64_t)   \
    X(i64_store16, uint16_t, uint64_t) \
    X(i64_store32, uint32_t, uint64_t) \
    X(v128_store, uint128_t, uint128_t)

// X(opcode, operation, type, resultType)
#define ENUMERATE_UNARY_OPERATIONS(X)                                                            \
    X(i32_eqz, eqz, uint32_t, uint32_t)                                                          \
    X(i64_eqz, eqz, uint64_t, uint32_t)                                                          \
    X(i32_clz, clz, uint32_t, uint32_t)                                                          \
    X(i32_ctz, ctz, uint32_t, uint32_t)                                                          \
    X(i32_popcnt, popcnt, uint32_t, uint32_t)                                                    \
    X(i64_clz, clz, uint64_t, uint64_t)                                                          \
    X(i64_ctz, ctz, uint64_t, uint64_t)                                                          \
    X(i64_popcnt, popcnt, uint64_t, uint64_t)                                                    \
    X(f32_abs, abs, float, float)                                                                \
    X(f32_neg, neg, float, float)                                                                \
    X(f32_ceil, ceil, float, float)                                                              \
    X(f32_floor, floor, float, float)                                                            \
    X(f32_trunc, trunc, float, float)                                                            \
    X(f32_nearest, nearest, float, float)                                                        \
    X(f32_sqrt, sqrt, float, float)                                                              \
    X(f64_abs, abs, double, double)                                                              \
    X(f64_neg, neg, double, double)                                                              \
    X(f64_ceil, ceil, double, double)                                                            \
    X(f64_floor, floor, double, double)                                                          \
    X(f64_trunc, trunc, double, double)                                                          \
    X(f64_nearest, nearest, double, double)                                                      \
    X(f64_sqrt, sqrt, double, double)                                                            \
    X(i32_wrap_i64, convert_u<uint32_t>, uint64_t, uint32_t)                                     \
    X(i32_trunc_f32_s, trunc<int32_t>, float, int32_t)                                           \
    X(i32_trunc_f32_u, trunc<uint32_t>, float, uint32_t)                                         \
    X(i32_trunc_f64_s, trunc<int32_t>, double, int32_t)                                          \
    X(i32_trunc_f64_u, trunc<uint32_t>, double, uint32_t)                                        \
    X(i64_extend_i32_s, convert_s<uint64_t>, uint32_t, uint64_t)                                 \
    X(i64_extend_i32_u, convert_u<uint64_t>, uint32_t, uint64_t)                                 \
    X(i64_trunc_f32_s, trunc<int64_t>, float, int64_t)                                           \
    X(i64_trunc_f32_u, trunc<uint64_t>, float, uint64_t)                                         \
    X(i64_trunc_f64_s, trunc<int64_t>, double, int64_t)                                          \
    X(i64_trunc_f64_u, trunc<uint64_t>, double, uint64_t)                                        \
    X(f32_convert_i32_s, convert_s<float>, uint32_t, float)                                      \
    X(f32_convert_i32_u, convert_u<float>, uint32_t, float)                                      \
    X(f32_convert_i64_s, convert_s<float>, uint64_t, float)                                      \
    X(f32_convert_i64_u, convert_u<float>, uint64_t, float)                                      \
    X(f32_demote_f64, convert_u<float>, double, float)                                           \
    X(f64_convert_i32_s, convert_s<double>, uint32_t, double)                                    \
    X(f64_convert_i32_u, convert_u<double>, uint32_t, double)                                    \
    X(f64_convert_i64_s, convert_s<double>, uint64_t, double)                                    \
    X(f64_convert_i64_u, convert_u<double>, uint64_t, double)                                    \
    X(f64_promote_f32, convert_u<double>, float, double)                                         \
    X(i32_reinterpret_f32, reinterpret<uint32_t>, float, uint32_t)                               \
    X(i64_reinterpret_f64, reinterpret<uint64_t>, double, uint64_t)                              \
    X(f32_reinterpret_i32, reinterpret<float>, uint32_t, float)                                  \
    X(f64_reinterpret_i64, reinterpret<double>, uint64_t, double)                                \
    X(i32_extend8_s, extend<uint8_t>, uint32_t, uint32_t)                                        \
    X(i32_extend16_s, extend<uint16_t>, uint32_t, uint32_t)                                      \
    X(i64_extend8_s, extend<uint8_t>, uint64_t, uint64_t)                                        \
    X(i64_extend16_s, extend<uint16_t>, uint64_t, uint64_t)                                      \
    X(i64_extend32_s, extend<uint32_t>, uint64_t, uint64_t)                                      \
    X(i32_trunc_sat_f32_s, trunc_sat<int32_t>, float, int32_t)                                   \
    X(i32_trunc_sat_f32_u, trunc_sat<uint32_t>, float, uint32_t)                                 \
    X(i32_trunc_sat_f64_s, trunc_sat<int32_t>, double, int32_t)                                  \
    X(i32_trunc_sat_f64_u, trunc_sat<uint32_t>, double, uint32_t)                                \
    X(i64_trunc_sat_f32_s, trunc_sat<int64_t>, float, int64_t)                                   \
    X(i64_trunc_sat_f32_u, trunc_sat<uint64_t>, float, uint64_t)                                 \
    X(i64_trunc_sat_f64_s, trunc_sat<int64_t>, double, int64_t)                                  \
    X(i64_trunc_sat_f64_u, trunc_sat<uint64_t>, double, uint64_t)                                \
    X(i8x16_splat, vector_broadcast<uint8x16_t>, uint32_t, uint8x16_t)                           \
    X(i16x8_splat, vector_broadcast<uint16x8_t>, uint32_t, uint16x8_t)                           \
    X(i32x4_splat, vector_broadcast<uint32x4_t>, uint32_t, uint32x4_t)                           \
    X(i64x2_splat, vector_broadcast<uint64x2_t>, uint64_t, uint64x2_t)                           \
    X(f32x4_splat, vector_broadcast<float32x4_t>, float, float32x4_t)                            \
    X(f64x2_splat, vector_broadcast<float64x2_t>, double, float64x2_t)                           \
    X(v128_not, not, uint128_t, uint128_t)                                                       \
    X(v128_any_true, any_true, uint128_t, uint32_t)                                              \
    X(f32x4_demote_f64x2_zero, vector_demote<float32x4_t>, float64x2_t, float32x4_t)             \
    X(f64x2_promote_low_f32x4, vector_promote<float64x2_t>, float32x4_t, float64x2_t)            \
    X(i8x16_abs, vector_abs, int8x16_t, int8x16_t)                                               \
    X(i8x16_neg, neg, int8x16_t, int8x16_t)                                                      \
    X(i8x16_popcnt, vector_popcnt, uint8x16_t, uint8x16_t)                                       \
    X(i8x16_all_true, vector_all_true, uint8x16_t, uint32_t)                                     \
    X(i8x16_bitmask, vector_bitmask, int8x16_t, uint32_t)                                        \
    X(f32x4_ceil, vector_ceil, float32x4_t, float32x4_t)                                         \
    X(f32x4_floor, vector_floor, float32x4_t, float32x4_t)                                       \
    X(f32x4_trunc, vector_trunc, float32x4_t, float32x4_t)                                       \
    X(f32x4_nearest, vector_nearest, float32x4_t, float32x4_t)                                   \
    X(f32x4_sqrt, vector_sqrt, float32x4_t, float32x4_t)                                         \
    X(f64x2_ceil, vector_ceil, float64x2_t, float64x2_t)                                         \
    X(f64x2_floor, vector_floor, float64x2_t, float64x2_t)                                       \
    X(f64x2_trunc, vector_trunc, float64x2_t, float64x2_t)                                       \
    X(i16x8_abs, vector_abs, int16x8_t, int16x8_t)                                               \
    X(i16x8_neg, neg, int16x8_t, int16x8_t)                                                      \
    X(i16x8_all_true, vector_all_true, uint16x8_t, uint32_t)                                     \
    X(i16x8_bitmask, vector_bitmask, int16x8_t, uint32_t)                                        \
    X(f64x2_nearest, vector_nearest, float64x2_t, float64x2_t)                                   \
    X(f64x2_sqrt, vector_sqrt, float64x2_t, float64x2_t)                                         \
    X(i16x8_extadd_pairwise_i8x16_s, vector_extadd_pairwise<int16x8_t>, int8x16_t, int16x8_t)    \
    X(i16x8_extadd_pairwise_i8x16_u, vector_extadd_pairwise<uint16x8_t>, uint8x16_t, uint16x8_t) \
    X(i32x4_extadd_pairwise_i16x8_s, vector_extadd_pairwise<int32x4_t>, int16x8_t, int32x4_t)    \
    X(i32x4_extadd_pairwise_i16x8_u, vector_extadd_pairwise<uint32x4_t>, uint16x8_t, uint32x4_t) \
    X(i32x4_abs, vector_abs, int32x4_t, int32x4_t)                                               \
    X(i32x4_neg, neg, int32x4_t, int32x4_t)                                                      \
    X(i32x4_all_true, vector_all_true, uint32x4_t, uint32_t)                                     \
    X(i32x4_bitmask, vector_bitmask, int32x4_t, uint32_t)                                        \
    X(i64x2_abs, vector_abs, int64x2_t, int64x2_t)                                               \
    X(i64x2_neg, neg, int64x2_t, int64x2_t)                                                      \
    X(i64x2_all_true, vector_all_true, uint64x2_t, uint32_t)                                     \
    X(i64x2_bitmask, vector_bitmask, int64x2_t, uint32_t)                                        \
    X(f32x4_abs, vector_abs, float32x4_t, float32x4_t)                                           \
    X(f32x4_neg, neg, float32x4_t, float32x4_t)                                                  \
    X(f64x2_abs, vector_abs, float64x2_t, float64x2_t)                                           \
    X(f64x2_neg, neg, float64x2_t, float64x2_t)                                                  \
    X(i16x8_extend_low_i8x16_s, vector_extend_low<int16x8_t>, int8x16_t, int16x8_t)              \
    X(i16x8_extend_high_i8x16_s, vector_extend_high<int16x8_t>, int8x16_t, int16x8_t)            \
    X(i16x8_extend_low_i8x16_u, vector_extend_low<uint16x8_t>, uint8x16_t, uint16x8_t)           \
    X(i16x8_extend_high_i8x16_u, vector_extend_high<uint16x8_t>, uint8x16_t, uint16x8_t)         \
    X(i32x4_extend_low_i16x8_s, vector_extend_low<int32x4_t>, int16x8_t, int32x4_t)              \
    X(i32x4_extend_high_i16x8_s, vector_extend_high<int32x4_t>, int16x8_t, int32x4_t)            \
    X(i32x4_extend_low_i16x8_u, vector_extend_low<uint32x4_t>, uint16x8_t, uint32x4_t)           \
    X(i32x4_extend_high_i16x8_u, vector_extend_high<uint32x4_t>, uint16x8_t, uint32x4_t)         \
    X(i64x2_extend_low_i32x4_s, vector_extend_low<int64x2_t>, int32x4_t, int64x2_t)              \
    X(i64x2_extend_high_i32x4_s, vector_extend_high<int64x2_t>, int32x4_t, int64x2_t)            \
    X(i64x2_extend_low_i32x4_u, vector_extend_low<uint64x2_t>, uint32x4_t, uint64x2_t)           \
    X(i64x2_extend_high_i32x4_u, vector_extend_high<uint64x2_t>, uint32x4_t, uint64x2_t)         \
    X(i32x4_trunc_sat_f32x4_s, vector_trunc_sat<int32x4_t>, float32x4_t, int32x4_t)              \
    X(i32x4_trunc_sat_f32x4_u, vector_trunc_sat<uint32x4_t>, float32x4_t, uint32x4_t)            \
    X(f32x4_convert_i32x4_s, vector_convert<float32x4_t>, int32x4_t, float32x4_t)                \
    X(f32x4_convert_i32x4_u, vector_convert<float32x4_t>, uint32x4_t, float32x4_t)               \
    X(i32x4_trunc_sat_f64x2_s_zero, vector_trunc_sat<int32x4_t>, float64x2_t, int32x4_t)         \
    X(i32x4_trunc_sat_f64x2_u_zero, vector_trunc_sat<uint32x4_t>, float64x2_t, uint32x4_t)       \
    X(f64x2_convert_low_i32x4_s, vector_convert<float64x2_t>, int32x4_t, float64x2_t)            \
    X(f64x2_convert_low_i32x4_u, vector_convert<float64x2_t>, uint32x4_t, float64x2_t)           \
    X(i32x4_relaxed_trunc_f32x4_s, vector_trunc_sat<int32x4_t>, float32x4_t, int32x4_t)          \
    X(i32x4_relaxed_trunc_f32x4_u, vector_trunc_sat<uint32x4_t>, float32x4_t, uint32x4_t)        \
    X(i32x4_relaxed_trunc_f64x2_s_zero, vector_trunc_sat<int32x4_t>, float64x2_t, int32x4_t)     \
    X(i32x4_relaxed_trunc_f64x2_u_zero, vector_trunc_sat<uint32x4_t>, float64x2_t, uint32x4_t)

// X(opcode, operation, lhsType, rhsType, resultType)
#define ENUMERATE_BINARY_OPERATIONS(X)                                                                        \
    X(i32_eq, eq, uint32_t, int32_t, uint32_t)                                                                \
    X(i32_ne, ne, uint32_t, int32_t, uint32_t)                                                                \
    X(i32_lt_s, lt, int32_t, int32_t, uint32_t)                                                               \
    X(i32_lt_u, lt, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_gt_s, gt, int32_t, int32_t, uint32_t)                                                               \
    X(i32_gt_u, gt, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_le_s, le, int32_t, int32_t, uint32_t)                                                               \
    X(i32_le_u, le, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_ge_s, ge, int32_t, int32_t, uint32_t)                                                               \
    X(i32_ge_u, ge, uint32_t, int32_t, uint32_t)                                                              \
    X(i64_eq, eq, uint64_t, int64_t, uint32_t)                                                                \
    X(i64_ne, ne, uint64_t, int64_t, uint32_t)                                                                \
    X(i64_lt_s, lt, int64_t, int64_t, uint32_t)                                                               \
    X(i64_lt_u, lt, uint64_t, int64_t, uint32_t)                                                              \
    X(i64_gt_s, gt, int64_t, int64_t, uint32_t)                                                               \
    X(i64_gt_u, gt, uint64_t, int64_t, uint32_t)                                                              \
    X(i64_le_s, le, int64_t, int64_t, uint32_t)                                                               \
    X(i64_le_u, le, uint64_t, int64_t, uint32_t)                                                              \
    X(i64_ge_s, ge, int64_t, int64_t, uint32_t)                                                               \
    X(i64_ge_u, ge, uint64_t, int64_t, uint32_t)                                                              \
    X(f32_eq, eq, float, float, uint32_t)                                                                     \
    X(f32_ne, ne, float, float, uint32_t)                                                                     \
    X(f32_lt, lt, float, float, uint32_t)                                                                     \
    X(f32_gt, gt, float, float, uint32_t)                                                                     \
    X(f32_le, le, float, float, uint32_t)                                                                     \
    X(f32_ge, ge, float, float, uint32_t)                                                                     \
    X(f64_eq, eq, double, double, uint32_t)                                                                   \
    X(f64_ne, ne, double, double, uint32_t)                                                                   \
    X(f64_lt, lt, double, double, uint32_t)                                                                   \
    X(f64_gt, gt, double, double, uint32_t)                                                                   \
    X(f64_le, le, double, double, uint32_t)                                                                   \
    X(f64_ge, ge, double, double, uint32_t)                                                                   \
    X(i32_add, add, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_sub, sub, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_mul, mul, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_div_s, div, int32_t, int32_t, int32_t)                                                              \
    X(i32_div_u, div, uint32_t, int32_t, uint32_t)                                                            \
    X(i32_rem_s, rem, int32_t, int32_t, int32_t)                                                              \
    X(i32_rem_u, rem, uint32_t, int32_t, uint32_t)                                                            \
    X(i32_and, and, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_or, or, uint32_t, int32_t, uint32_t)                                                                \
    X(i32_xor, xor, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_shl, shl, uint32_t, int32_t, uint32_t)                                                              \
    X(i32_shr_s, shr, int32_t, int32_t, int32_t)                                                              \
    X(i32_shr_u, shr, uint32_t, int32_t, uint32_t)                                                            \
    X(i32_rotl, rotl, uint32_t, int32_t, uint32_t)                                                            \
    X(i32_rotr, rotr, uint32_t, int32_t, uint32_t)                                                            \
    X(i64_add, add, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_sub, sub, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_mul, mul, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_div_s, div, int64_t, int64_t, int64_t)                                                              \
    X(i64_div_u, div, uint64_t, int64_t, uint64_t)                                                            \
    X(i64_rem_s, rem, int64_t, int64_t, int64_t)                                                              \
    X(i64_rem_u, rem, uint64_t, int64_t, uint64_t)                                                            \
    X(i64_and, and, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_or, or, uint64_t, int64_t, uint64_t)                                                                \
    X(i64_xor, xor, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_shl, shl, uint64_t, int64_t, uint64_t)                                                              \
    X(i64_shr_s, shr, int64_t, int64_t, int64_t)                                                              \
    X(i64_shr_u, shr, uint64_t, int64_t, uint64_t)                                                            \
    X(i64_rotl, rotl, uint64_t, uint64_t, uint64_t)                                                           \
    X(i64_rotr, rotr, uint64_t, uint64_t, uint64_t)                                                           \
    X(f32_add, add, float, float, float)                                                                      \
    X(f32_sub, sub, float, float, float)                                                                      \
    X(f32_mul, mul, float, float, float)                                                                      \
    X(f32_div, div, float, float, float)                                                                      \
    X(f32_min, min, float, float, float)                                                                      \
    X(f32_max, max, float, float, float)                                                                      \
    X(f32_copysign, copysign, float, float, float)                                                            \
    X(f64_add, add, double, double, double)                                                                   \
    X(f64_sub, sub, double, double, double)                                                                   \
    X(f64_mul, mul, double, double, double)                                                                   \
    X(f64_div, div, double, double, double)                                                                   \
    X(f64_min, min, double, double, double)                                                                   \
    X(f64_max, max, double, double, double)                                                                   \
    X(f64_copysign, copysign, double, double, double)                                                         \
    X(i8x16_swizzle, vector_swizzle, uint8x16_t, uint8x16_t, uint8x16_t)                                      \
    X(i8x16_eq, eq, uint8x16_t, uint8x16_t, uint8x16_t)                                                       \
    X(i8x16_ne, ne, uint8x16_t, uint8x16_t, uint8x16_t)                                                       \
    X(i8x16_lt_s, lt, int8x16_t, int8x16_t, int8x16_t)                                                        \
    X(i8x16_lt_u, lt, uint8x16_t, uint8x16_t, uint8x16_t)                                                     \
    X(i8x16_gt_s, gt, int8x16_t, int8x16_t, int8x16_t)                                                        \
    X(i8x16_gt_u, gt, uint8x16_t, uint8x16_t, uint8x16_t)                                                     \
    X(i8x16_le_s, le, int8x16_t, int8x16_t, int8x16_t)                                                        \
    X(i8x16_le_u, le, uint8x16_t, uint8x16_t, uint8x16_t)                                                     \
    X(i8x16_ge_s, ge, int8x16_t, int8x16_t, int8x16_t)                                                        \
    X(i8x16_ge_u, ge, uint8x16_t, uint8x16_t, uint8x16_t)                                                     \
    X(i16x8_eq, eq, uint16x8_t, uint16x8_t, uint16x8_t)                                                       \
    X(i16x8_ne, ne, uint16x8_t, uint16x8_t, uint16x8_t)                                                       \
    X(i16x8_lt_s, lt, int16x8_t, int16x8_t, int16x8_t)                                                        \
    X(i16x8_lt_u, lt, uint16x8_t, uint16x8_t, uint16x8_t)                                                     \
    X(i16x8_gt_s, gt, int16x8_t, int16x8_t, int16x8_t)                                                        \
    X(i16x8_gt_u, gt, uint16x8_t, uint16x8_t, uint16x8_t)                                                     \
    X(i16x8_le_s, le, int16x8_t, int16x8_t, int16x8_t)                                                        \
    X(i16x8_le_u, le, uint16x8_t, uint16x8_t, uint16x8_t)                                                     \
    X(i16x8_ge_s, ge, int16x8_t, int16x8_t, int16x8_t)                                                        \
    X(i16x8_ge_u, ge, uint16x8_t, uint16x8_t, uint16x8_t)                                                     \
    X(i32x4_eq, eq, uint32x4_t, uint32x4_t, uint32x4_t)                                                       \
    X(i32x4_ne, ne, uint32x4_t, uint32x4_t, uint32x4_t)                                                       \
    X(i32x4_lt_s, lt, int32x4_t, int32x4_t, int32x4_t)                                                        \
    X(i32x4_lt_u, lt, uint32x4_t, uint32x4_t, uint32x4_t)                                                     \
    X(i32x4_gt_s, gt, int32x4_t, int32x4_t, int32x4_t)                                                        \
    X(i32x4_gt_u, gt, uint32x4_t, uint32x4_t, uint32x4_t)                                                     \
    X(i32x4_le_s, le, int32x4_t, int32x4_t, int32x4_t)                                                        \
    X(i32x4_le_u, le, uint32x4_t, uint32x4_t, uint32x4_t)                                                     \
    X(i32x4_ge_s, ge, int32x4_t, int32x4_t, int32x4_t)                                                        \
    X(i32x4_ge_u, ge, uint32x4_t, uint32x4_t, uint32x4_t)                                                     \
    X(f32x4_eq, eq, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f32x4_ne, ne, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f32x4_lt, lt, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f32x4_gt, gt, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f32x4_le, le, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f32x4_ge, ge, float32x4_t, float32x4_t, float32x4_t)                                                    \
    X(f64x2_eq, eq, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(f64x2_ne, ne, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(f64x2_lt, lt, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(f64x2_gt, gt, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(f64x2_le, le, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(f64x2_ge, ge, float64x2_t, float64x2_t, float64x2_t)                                                    \
    X(v128_and, and, uint128_t, uint128_t, uint128_t)                                                         \
    X(v128_andnot, andnot, uint128_t, uint128_t, uint128_t)                                                   \
    X(v128_or, or, uint128_t, uint128_t, uint128_t)                                                           \
    X(v128_xor, xor, uint128_t, uint128_t, uint128_t)                                                         \
    X(i8x16_narrow_i16x8_s, vector_narrow<int8x16_t>, int16x8_t, int16x8_t, int8x16_t)                        \
    X(i8x16_narrow_i16x8_u, vector_narrow<uint8x16_t>, int16x8_t, int16x8_t, uint8x16_t)                      \
    X(i8x16_shl, vector_shl, uint8x16_t, uint32_t, uint8x16_t)                                                \
    X(i8x16_shr_s, vector_shr, int8x16_t, uint32_t, int8x16_t)                                                \
    X(i8x16_shr_u, vector_shr, uint8x16_t, uint32_t, uint8x16_t)                                              \
    X(i8x16_add, add, uint8x16_t, int8x16_t, uint8x16_t)                                                      \
    X(i8x16_add_sat_s, vector_add_sat, int8x16_t, int8x16_t, int8x16_t)                                       \
    X(i8x16_add_sat_u, vector_add_sat, uint8x16_t, uint8x16_t, uint8x16_t)                                    \
    X(i8x16_sub, sub, uint8x16_t, int8x16_t, uint8x16_t)                                                      \
    X(i8x16_sub_sat_s, vector_sub_sat, int8x16_t, int8x16_t, int8x16_t)                                       \
    X(i8x16_sub_sat_u, vector_sub_sat, uint8x16_t, uint8x16_t, uint8x16_t)                                    \
    X(i8x16_min_s, vector_min, int8x16_t, int8x16_t, int8x16_t)                                               \
    X(i8x16_min_u, vector_min, uint8x16_t, uint8x16_t, uint8x16_t)                                            \
    X(i8x16_max_s, vector_max, int8x16_t, int8x16_t, int8x16_t)                                               \
    X(i8x16_max_u, vector_max, uint8x16_t, uint8x16_t, uint8x16_t)                                            \
    X(i8x16_avgr_u, vector_avgr, uint8x16_t, uint8x16_t, uint8x16_t)                                          \
    X(i16x8_q15mulr_sat_s, vector_q15mulr_sat, int16x8_t, int16x8_t, int16x8_t)                               \
    X(i16x8_narrow_i32x4_s, vector_narrow<int16x8_t>, int32x4_t, int32x4_t, int16x8_t)                        \
    X(i16x8_narrow_i32x4_u, vector_narrow<uint16x8_t>, int32x4_t, int32x4_t, uint16x8_t)                      \
    X(i16x8_shl, vector_shl, uint16x8_t, uint32_t, uint16x8_t)                                                \
    X(i16x8_shr_s, vector_shr, int16x8_t, uint32_t, int16x8_t)                                                \
    X(i16x8_shr_u, vector_shr, uint16x8_t, uint32_t, uint16x8_t)                                              \
    X(i16x8_add, add, uint16x8_t, int16x8_t, uint16x8_t)                                                      \
    X(i16x8_add_sat_s, vector_add_sat, int16x8_t, int16x8_t, int16x8_t)                                       \
    X(i16x8_add_sat_u, vector_add_sat, uint16x8_t, uint16x8_t, uint16x8_t)                                    \
    X(i16x8_sub, sub, uint16x8_t, int16x8_t, uint16x8_t)                                                      \
    X(i16x8_sub_sat_s, vector_sub_sat, int16x8_t, int16x8_t, int16x8_t)                                       \
    X(i16x8_sub_sat_u, vector_sub_sat, uint16x8_t, uint16x8_t, uint16x8_t)                                    \
    X(i16x8_mul, mul, uint16x8_t, int16x8_t, uint16x8_t)                                                      \
    X(i16x8_min_s, vector_min, int16x8_t, int16x8_t, int16x8_t)                                               \
    X(i16x8_min_u, vector_min, uint16x8_t, uint16x8_t, uint16x8_t)                                            \
    X(i16x8_max_s, vector_max, int16x8_t, int16x8_t, int16x8_t)                                               \
    X(i16x8_max_u, vector_max, uint16x8_t, uint16x8_t, uint16x8_t)                                            \
    X(i16x8_avgr_u, vector_avgr, uint16x8_t, uint16x8_t, uint16x8_t)                                          \
    X(i32x4_shl, vector_shl, uint32x4_t, uint32_t, uint32x4_t)                                                \
    X(i32x4_shr_s, vector_shr, int32x4_t, uint32_t, int32x4_t)                                                \
    X(i32x4_shr_u, vector_shr, uint32x4_t, uint32_t, uint32x4_t)                                              \
    X(i32x4_add, add, uint32x4_t, int32x4_t, uint32x4_t)                                                      \
    X(i32x4_sub, sub, uint32x4_t, int32x4_t, uint32x4_t)                                                      \
    X(i32x4_mul, mul, uint32x4_t, int32x4_t, uint32x4_t)                                                      \
    X(i32x4_min_s, vector_min, int32x4_t, int32x4_t, int32x4_t)                                               \
    X(i32x4_min_u, vector_min, uint32x4_t, uint32x4_t, uint32x4_t)                                            \
    X(i32x4_max_s, vector_max, int32x4_t, int32x4_t, int32x4_t)                                               \
    X(i32x4_max_u, vector_max, uint32x4_t, uint32x4_t, uint32x4_t)                                            \
    X(i32x4_dot_i16x8_s, vector_dot, int16x8_t, int16x8_t, int32x4_t)                                         \
    X(i64x2_shl, vector_shl, uint64x2_t, uint32_t, uint64x2_t)                                                \
    X(i64x2_shr_s, vector_shr, int64x2_t, uint32_t, int64x2_t)                                                \
    X(i64x2_shr_u, vector_shr, uint64x2_t, uint32_t, uint64x2_t)                                              \
    X(i64x2_add, add, uint64x2_t, int64x2_t, uint64x2_t)                                                      \
    X(i64x2_sub, sub, uint64x2_t, int64x2_t, uint64x2_t)                                                      \
    X(i64x2_mul, mul, uint64x2_t, int64x2_t, uint64x2_t)                                                      \
    X(i64x2_eq, eq, uint64x2_t, int64x2_t, uint64x2_t)                                                        \
    X(i64x2_ne, ne, uint64x2_t, int64x2_t, uint64x2_t)                                                        \
    X(i64x2_lt_s, lt, int64x2_t, int64x2_t, int64x2_t)                                                        \
    X(i64x2_gt_s, gt, int64x2_t, int64x2_t, int64x2_t)                                                        \
    X(i64x2_le_s, le, int64x2_t, int64x2_t, int64x2_t)                                                        \
    X(i64x2_ge_s, ge, int64x2_t, int64x2_t, int64x2_t)                                                        \
    X(f32x4_add, add, float32x4_t, float32x4_t, float32x4_t)                                                  \
    X(f32x4_sub, sub, float32x4_t, float32x4_t, float32x4_t)                                                  \
    X(f32x4_mul, mul, float32x4_t, float32x4_t, float32x4_t)                                                  \
    X(f32x4_div, div, float32x4_t, float32x4_t, float32x4_t)                                                  \
    X(f32x4_min, vector_min, float32x4_t, float32x4_t, float32x4_t)                                           \
    X(f32x4_max, vector_max, float32x4_t, float32x4_t, float32x4_t)                                           \
    X(f32x4_pmin, vector_pmin, float32x4_t, float32x4_t, float32x4_t)                                         \
    X(f32x4_pmax, vector_pmax, float32x4_t, float32x4_t, float32x4_t)                                         \
    X(f64x2_add, add, float64x2_t, float64x2_t, float64x2_t)                                                  \
    X(f64x2_sub, sub, float64x2_t, float64x2_t, float64x2_t)                                                  \
    X(f64x2_mul, mul, float64x2_t, float64x2_t, float64x2_t)                                                  \
    X(f64x2_div, div, float64x2_t, float32x4_t, float64x2_t)                                                  \
    X(f64x2_min, vector_min, float64x2_t, float64x2_t, float64x2_t)                                           \
    X(f64x2_max, vector_max, float64x2_t, float64x2_t, float64x2_t)                                           \
    X(f64x2_pmin, vector_pmin, float64x2_t, float64x2_t, float64x2_t)                                         \
    X(f64x2_pmax, vector_pmax, float64x2_t, float64x2_t, float64x2_t)                                         \
    X(i16x8_extmul_low_i8x16_s, vector_extend_multiply_low<int16x8_t>, int8x16_t, int8x16_t, int16x8_t)       \
    X(i16x8_extmul_high_i8x16_s, vector_extend_multiply_high<int16x8_t>, int8x16_t, int8x16_t, int16x8_t)     \
    X(i16x8_extmul_low_i8x16_u, vector_extend_multiply_low<uint16x8_t>, uint8x16_t, uint8x16_t, uint16x8_t)   \
    X(i16x8_extmul_high_i8x16_u, vector_extend_multiply_high<uint16x8_t>, uint8x16_t, uint8x16_t, uint16x8_t) \
    X(i32x4_extmul_low_i16x8_s, vector_extend_multiply_low<int32x4_t>, int16x8_t, int16x8_t, int32x4_t)       \
    X(i32x4_extmul_high_i16x8_s, vector_extend_multiply_high<int32x4_t>, int16x8_t, int16x8_t, int32x4_t)     \
    X(i32x4_extmul_low_i16x8_u, vector_extend_multiply_low<uint32x4_t>, uint16x8_t, uint16x8_t, uint32x4_t)   \
    X(i32x4_extmul_high_i16x8_u, vector_extend_multiply_high<uint32x4_t>, uint16x8_t, uint16x8_t, uint32x4_t) \
    X(i64x2_extmul_low_i32x4_s, vector_extend_multiply_low<int64x2_t>, int32x4_t, int32x4_t, int64x2_t)       \
    X(i64x2_extmul_high_i32x4_s, vector_extend_multiply_high<int64x2_t>, int32x4_t, int32x4_t, int64x2_t)     \
    X(i64x2_extmul_low_i32x4_u, vector_extend_multiply_low<uint64x2_t>, uint32x4_t, uint32x4_t, uint64x2_t)   \
    X(i64x2_extmul_high_i32x4_u, vector_extend_multiply_high<uint64x2_t>, uint32x4_t, uint32x4_t, uint64x2_t) \
    X(i8x16_relaxed_swizzle, vector_swizzle, uint8x16_t, uint8x16_t, uint8x16_t)                              \
    X(f32x4_relaxed_min, vector_min, float32x4_t, float32x4_t, float32x4_t)                                   \
    X(f32x4_relaxed_max, vector_max, float32x4_t, float32x4_t, float32x4_t)                                   \
    X(f64x2_relaxed_min, vector_min, float64x2_t, float64x2_t, float64x2_t)                                   \
    X(f64x2_relaxed_max, vector_max, float64x2_t, float64x2_t, float64x2_t)                                   \
    X(i16x8_relaxed_q15mulr_s, vector_q15mulr_sat, int16x8_t, int16x8_t, int16x8_t)
