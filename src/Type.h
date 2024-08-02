#pragma once

#include <Value.h>
#include <cstdint>

enum Type
{
    i32 = 0x7F,
    i64 = 0x7E,
    f32 = 0x7D,
    f64 = 0x7C,
    funcref = 0x70,
    externref = 0x6F,
};

constexpr uint8_t EMPTY_TYPE = 0x40;

bool is_valid_type(uint8_t type);
Value default_value_for_type(uint8_t type);
Type get_value_type(Value value);
ReferenceType get_reference_type_from_reftype(uint8_t type);
