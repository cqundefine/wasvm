#pragma once

#include <Stream.h>
#include <Value.h>
#include <cstdint>

enum class Type
{
    i32 = 0x7F,
    i64 = 0x7E,
    f32 = 0x7D,
    f64 = 0x7C,
    v128 = 0x7B,
    funcref = 0x70,
    externref = 0x6F,
    empty = 0x40,
};

Type read_type_from_stream(Stream&);
bool is_valid_type(Type type);
Value default_value_for_type(Type type);
Type get_value_type(Value value);
ReferenceType get_reference_type_from_reftype(Type type);
std::string get_type_name(Type type);
Value::Type value_type_from_type(Type type);
