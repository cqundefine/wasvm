#pragma once

#include <SIMD.h>
#include <Util.h>
#include <Value.h>
#include <bit>
#include <cassert>
#include <cmath>
#include <type_traits>

#define GENERIC_BINARY_OPERATION_OPERATOR(name, op)         \
    template <typename LhsType, typename RhsType>           \
    Value operation_##name(LhsType a, RhsType b)            \
    {                                                       \
        return std::bit_cast<ToValueType<LhsType>>(a op b); \
    }

#define GENERIC_BINARY_OPERATION_FUNCTION(name, function)           \
    template <typename LhsType, typename RhsType>                   \
    Value operation_##name(LhsType a, RhsType b)                    \
    {                                                               \
        return std::bit_cast<ToValueType<LhsType>>(function(a, b)); \
    }

#define GENERIC_COMPARISON_OPERATION_OPERATOR(name, op)    \
    template <typename LhsType, typename RhsType>          \
        requires(!IsVector<LhsType> && !IsVector<RhsType>) \
    Value operation_##name(LhsType a, RhsType b)           \
    {                                                      \
        return (uint32_t)(a op b);                         \
    }                                                      \
    template <typename LhsType, typename RhsType>          \
        requires(IsVector<LhsType> && IsVector<RhsType>)   \
    Value operation_##name(LhsType a, RhsType b)           \
    {                                                      \
        return std::bit_cast<uint128_t>(a op b);           \
    }

GENERIC_BINARY_OPERATION_OPERATOR(add, +);
GENERIC_BINARY_OPERATION_OPERATOR(sub, -);
GENERIC_BINARY_OPERATION_OPERATOR(mul, *);
GENERIC_BINARY_OPERATION_OPERATOR(and, &);
GENERIC_BINARY_OPERATION_OPERATOR(or, |);
GENERIC_BINARY_OPERATION_OPERATOR(xor, ^);
GENERIC_BINARY_OPERATION_OPERATOR(shl, <<);
GENERIC_BINARY_OPERATION_OPERATOR(shr, >>);
GENERIC_BINARY_OPERATION_FUNCTION(rotl, std::rotl);
GENERIC_BINARY_OPERATION_FUNCTION(rotr, std::rotr);
GENERIC_BINARY_OPERATION_FUNCTION(copysign, std::copysign);
GENERIC_COMPARISON_OPERATION_OPERATOR(eq, ==);
GENERIC_COMPARISON_OPERATION_OPERATOR(ne, !=);
GENERIC_COMPARISON_OPERATION_OPERATOR(lt, <);
GENERIC_COMPARISON_OPERATION_OPERATOR(gt, >);
GENERIC_COMPARISON_OPERATION_OPERATOR(le, <=);
GENERIC_COMPARISON_OPERATION_OPERATOR(ge, >=);

GENERIC_BINARY_OPERATION_FUNCTION(vector_min, vector_min);
GENERIC_BINARY_OPERATION_FUNCTION(vector_max, vector_max);

template <typename LhsType, typename RhsType>
Value operation_min(LhsType a, RhsType b)
{
    static_assert(std::is_floating_point<LhsType>() && std::is_floating_point<RhsType>(), "Opeartion min only supports floating point");

    if (std::isnan(a))
        return typed_nan<ToValueType<LhsType>>();

    if (std::isnan(b))
        return typed_nan<ToValueType<LhsType>>();

    return (ToValueType<LhsType>)std::min(a, b);
}

template <typename LhsType, typename RhsType>
Value operation_max(LhsType a, RhsType b)
{
    static_assert(std::is_floating_point<LhsType>() && std::is_floating_point<RhsType>(), "Opeartion max only supports floating point");

    if (std::isnan(a))
        return typed_nan<ToValueType<LhsType>>();

    if (std::isnan(b))
        return typed_nan<ToValueType<LhsType>>();

    return (ToValueType<LhsType>)std::max(a, b);
}

template <typename LhsType, typename RhsType>
Value operation_div(LhsType a, RhsType b)
{
    if constexpr (std::is_integral<LhsType>())
    {
        if constexpr (std::is_signed<LhsType>())
            if (a == ((ToValueType<LhsType>)1 << (sizeof(LhsType) * 8 - 1)) && b == -1)
                throw Trap();

        if (b == 0)
            throw Trap();
    }

    return (ToValueType<LhsType>)(a / b);
}

template <typename LhsType, typename RhsType>
Value operation_rem(LhsType a, RhsType b)
{
    if constexpr (std::is_signed<LhsType>())
        if (b == -1)
            return (ToValueType<LhsType>)0;

    if (b == 0)
        throw Trap();

    return (ToValueType<LhsType>)(a % b);
}

template <typename LhsType, typename RhsType>
Value operation_andnot(LhsType a, RhsType b)
{
    return a & ~b;
}

template <IsVector LhsType, typename RhsType>
Value operation_vector_shl(LhsType a, RhsType b)
{
    return a << (b % (sizeof(VectorElement<LhsType>) * 8));
}

template <IsVector LhsType, typename RhsType>
Value operation_vector_shr(LhsType a, RhsType b)
{
    return a >> (b % (sizeof(VectorElement<LhsType>) * 8));
}

template <IsVector LhsType, IsVector RhsType>
Value operation_vector_swizzle(LhsType a, RhsType b)
{
    // TODO: Use __builtin_shuffle on GCC
    LhsType result;
    constexpr auto laneCount = sizeof(LhsType) / sizeof(VectorElement<LhsType>);
    for (size_t i = 0; i < laneCount; i++)
    {
        if (b[i] < laneCount)
            result[i] = a[b[i]];
        else
            result[i] = 0;
    }
    return result;
}

template <IsVector LhsType, IsVector RhsType>
Value operation_vector_q15mulr_sat(LhsType a, RhsType b)
{
    // TODO: Find a SIMD instruction way to do this
    static_assert(std::is_same<LhsType, int16x8_t>() && std::is_same<RhsType, int16x8_t>(), "Unsupported vector type for q15mulr_sat");
    LhsType result;
    for (size_t i = 0; i < 8; i++)
        result[i] = (a[i] * b[i] + 0x4000) >> 15;
    return result;
}

#define GENERIC_UNARY_OPERATION_FUNCTION(name, function) \
    template <typename T>                                \
    Value operation_##name(T a)                          \
    {                                                    \
        return (ToValueType<T>)function(a);              \
    }

GENERIC_UNARY_OPERATION_FUNCTION(clz, std::countl_zero);
GENERIC_UNARY_OPERATION_FUNCTION(ctz, std::countr_zero);
GENERIC_UNARY_OPERATION_FUNCTION(popcnt, std::popcount);
GENERIC_UNARY_OPERATION_FUNCTION(abs, std::abs);
GENERIC_UNARY_OPERATION_FUNCTION(neg, -);
GENERIC_UNARY_OPERATION_FUNCTION(not, ~);
GENERIC_UNARY_OPERATION_FUNCTION(ceil, std::ceil);
GENERIC_UNARY_OPERATION_FUNCTION(floor, std::floor);
GENERIC_UNARY_OPERATION_FUNCTION(trunc, std::trunc);
GENERIC_UNARY_OPERATION_FUNCTION(nearest, std::nearbyint);
GENERIC_UNARY_OPERATION_FUNCTION(sqrt, std::sqrt);

GENERIC_UNARY_OPERATION_FUNCTION(vector_abs, vector_abs);
GENERIC_UNARY_OPERATION_FUNCTION(vector_ceil, vector_ceil);
GENERIC_UNARY_OPERATION_FUNCTION(vector_floor, vector_floor);
GENERIC_UNARY_OPERATION_FUNCTION(vector_trunc, vector_trunc);
GENERIC_UNARY_OPERATION_FUNCTION(vector_nearest, vector_nearest);

template <typename T>
Value operation_eqz(T a)
{
    return (uint32_t)(a == 0);
}

template <typename TruncatedType, typename T>
Value operation_trunc(T a)
{
    static_assert(std::is_floating_point<T>() && std::is_integral<TruncatedType>(), "operation_trunc is meant for floating point to integer conversions");

    if (std::isnan(a) || std::isinf(a))
        throw Trap();

    a = std::trunc(a);

    if (a < (std::is_signed<TruncatedType>() ? (-1 * std::pow(2.0, (sizeof(TruncatedType) * 8 - 1))) : 0))
        throw Trap();

    if (a > std::pow(2.0, (sizeof(TruncatedType) * 8 - std::is_signed<TruncatedType>())) - 1)
        throw Trap();

    return (ToValueType<TruncatedType>)(TruncatedType)a;
}

template <typename TruncatedType, typename T>
Value operation_trunc_sat(T a)
{
    static_assert(std::is_floating_point<T>() && std::is_integral<TruncatedType>(), "operation_trunc_sat is meant for floating point to integer conversions");

    if (std::isnan(a))
        return (ToValueType<TruncatedType>)0;

    if (std::isinf(a) && a < 0)
        return (ToValueType<TruncatedType>)std::numeric_limits<TruncatedType>().min();

    if (std::isinf(a) && a > 0)
        return (ToValueType<TruncatedType>)std::numeric_limits<TruncatedType>().max();

    a = std::trunc(a);

    if (a < (std::is_signed<TruncatedType>() ? (-1 * std::pow(2.0, (sizeof(TruncatedType) * 8 - 1))) : 0))
        return (ToValueType<TruncatedType>)std::numeric_limits<TruncatedType>().min();

    if (a > std::pow(2.0, (sizeof(TruncatedType) * 8 - std::is_signed<TruncatedType>())) - 1)
        return (ToValueType<TruncatedType>)std::numeric_limits<TruncatedType>().max();

    return (ToValueType<TruncatedType>)(TruncatedType)a;
}

template <typename ConvertedType, bool SignedResult, typename T>
Value operation_convert(T a)
{
    static_assert(std::is_floating_point<ConvertedType>() || std::is_unsigned<ConvertedType>(), "operation_convert takes a seperate parameter to indicate signed result");
    if constexpr (std::is_floating_point<ConvertedType>() && std::is_floating_point<T>())
        static_assert(!SignedResult, "operation_convert does not allow signed result with floating point to floating point, it's implied");

    if constexpr (SignedResult)
        return static_cast<ToValueType<ConvertedType>>(static_cast<ConvertedType>(static_cast<std::make_signed_t<T>>(a)));
    else
        return static_cast<ConvertedType>(a);
}

template <typename ConvertedType, typename T>
Value operation_convert_s(T a)
{
    return operation_convert<ConvertedType, true>(a);
}

template <typename ConvertedType, typename T>
Value operation_convert_u(T a)
{
    return operation_convert<ConvertedType, false>(a);
}

template <typename ReinterpretedType, typename T>
Value operation_reinterpret(T a)
{
    return std::bit_cast<ReinterpretedType>(a);
}

template <typename LowType, typename T>
Value operation_extend(T a)
{
    static_assert(std::is_unsigned<LowType>(), "operation_extend expects low type to be unsigned");
    return static_cast<T>(static_cast<std::make_signed_t<T>>(static_cast<std::make_signed_t<LowType>>(static_cast<LowType>(a))));
}

template <IsVector VectorType, typename T>
Value operation_vector_broadcast(T a)
{
    return vector_broadcast<VectorType>(static_cast<VectorElement<VectorType>>(a));
}

template <IsVector T>
Value operation_all_true(T a)
{
    constexpr auto laneCount = sizeof(T) / sizeof(VectorElement<T>);
    for (size_t i = 0; i < laneCount; i++)
        if (a[i] == 0)
            return (uint32_t)0;
    return (uint32_t)1;
}

template <IsVector T>
Value operation_bitmask(T a)
{
    constexpr auto laneCount = sizeof(T) / sizeof(VectorElement<T>);
    uint32_t result = 0;
    for (size_t i = 0; i < laneCount; i++)
        if (a[i] < 0)
            result |= (1 << i);
    return result;
}
