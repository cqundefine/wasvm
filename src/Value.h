#pragma once

#include <SIMD.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <variant>
#include <vector>

struct Trap
{
};

enum class LabelBeginType
{
    Else,
    Other,
    LoopInvalid
};

struct Label
{
    uint32_t continuation;
    uint32_t arity;
    LabelBeginType beginType;
};

enum class ReferenceType
{
    Function,
    Extern
};

struct Reference
{
    ReferenceType type;
    uint32_t index;

    Reference(ReferenceType type, uint32_t index)
        : type(type)
        , index(index)
    {
    }
};

typedef std::variant<uint32_t, uint64_t, float, double, uint128_t, Reference, Label> Value;

std::string value_to_string(Value value);

template <typename T>
extern const char* value_type_name;

template <>
extern const char* value_type_name<uint32_t>;

template <>
extern const char* value_type_name<uint64_t>;

template <>
extern const char* value_type_name<float>;

template <>
extern const char* value_type_name<double>;

template <>
extern const char* value_type_name<uint128_t>;

template <>
extern const char* value_type_name<Reference>;

template <>
extern const char* value_type_name<Label>;

template <typename T, typename = void>
struct ToValueTypeHelper
{
    using type = T;
};

template <typename T>
struct ToValueTypeHelper<T, std::enable_if_t<std::is_integral_v<T> && sizeof(T) <= 8>>
{
    using type = std::make_unsigned_t<T>;
};

template <typename T>
    requires IsVector<T>
struct ToValueTypeHelper<T>
{
    using type = uint128_t;
};

template <typename T>
using ToValueType = typename ToValueTypeHelper<T>::type;

static_assert(std::is_same<ToValueType<uint32_t>, uint32_t>());
static_assert(std::is_same<ToValueType<int32_t>, uint32_t>());
static_assert(std::is_same<ToValueType<float>, float>());
static_assert(std::is_same<ToValueType<Reference>, Reference>());
static_assert(std::is_same<ToValueType<uint128_t>, uint128_t>());
static_assert(std::is_same<ToValueType<int64x2_t>, uint128_t>());

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename T>
concept IsValueType = IsAnyOf<ToValueType<T>, uint32_t, uint64_t, float, double, uint128_t, Reference, Label>;
