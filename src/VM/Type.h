#pragma once

#include "Stream/Stream.h"
#include "Util/SIMD.h"
#include <cstdint>

enum class Type : uint8_t
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

template <typename T>
inline constexpr Type type_from_cpp_type = []() {
    static_assert(false);
};

template <>
inline constexpr Type type_from_cpp_type<uint32_t> = Type::i32;

template <>
inline constexpr Type type_from_cpp_type<uint64_t> = Type::i64;

template <>
inline constexpr Type type_from_cpp_type<float> = Type::f32;

template <>
inline constexpr Type type_from_cpp_type<double> = Type::f64;

template <>
inline constexpr Type type_from_cpp_type<uint128_t> = Type::v128;

class Value;
enum class ReferenceType;

Type read_type_from_stream(Stream&);
bool is_valid_type(Type type);
Value default_value_for_type(Type type);
ReferenceType get_reference_type_from_reftype(Type type);
std::string get_type_name(Type type);
bool is_reference_type(Type type);
