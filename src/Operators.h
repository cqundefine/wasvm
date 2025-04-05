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
    constexpr Value operation_##name(LhsType a, RhsType b)  \
    {                                                       \
        return std::bit_cast<ToValueType<LhsType>>(a op b); \
    }

#define GENERIC_BINARY_OPERATION_FUNCTION(name, function)           \
    template <typename LhsType, typename RhsType>                   \
    constexpr Value operation_##name(LhsType a, RhsType b)          \
    {                                                               \
        return std::bit_cast<ToValueType<LhsType>>(function(a, b)); \
    }

#define GENERIC_COMPARISON_OPERATION_OPERATOR(name, op)    \
    template <typename LhsType, typename RhsType>          \
        requires(!IsVector<LhsType> && !IsVector<RhsType>) \
    constexpr Value operation_##name(LhsType a, RhsType b) \
    {                                                      \
        return (uint32_t)(a op b);                         \
    }                                                      \
    template <typename LhsType, typename RhsType>          \
        requires(IsVector<LhsType> && IsVector<RhsType>)   \
    constexpr Value operation_##name(LhsType a, RhsType b) \
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
GENERIC_BINARY_OPERATION_FUNCTION(vector_pmin, vector_pmin);
GENERIC_BINARY_OPERATION_FUNCTION(vector_pmax, vector_pmax);
GENERIC_BINARY_OPERATION_FUNCTION(vector_avgr, vector_avgr);
GENERIC_BINARY_OPERATION_FUNCTION(vector_add_sat, vector_add_sat);
GENERIC_BINARY_OPERATION_FUNCTION(vector_sub_sat, vector_sub_sat);

template <typename LhsType, typename RhsType>
constexpr Value operation_min(LhsType a, RhsType b)
{
    static_assert(std::is_floating_point<LhsType>() && std::is_floating_point<RhsType>(), "Opeartion min only supports floating point");

    if (std::isnan(a))
        return typed_nan<ToValueType<LhsType>>();

    if (std::isnan(b))
        return typed_nan<ToValueType<LhsType>>();

    return (ToValueType<LhsType>)std::min(a, b);
}

template <typename LhsType, typename RhsType>
constexpr Value operation_max(LhsType a, RhsType b)
{
    static_assert(std::is_floating_point<LhsType>() && std::is_floating_point<RhsType>(), "Opeartion max only supports floating point");

    if (std::isnan(a))
        return typed_nan<ToValueType<LhsType>>();

    if (std::isnan(b))
        return typed_nan<ToValueType<LhsType>>();

    return (ToValueType<LhsType>)std::max(a, b);
}

template <typename LhsType, typename RhsType>
constexpr Value operation_div(LhsType a, RhsType b)
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
constexpr Value operation_rem(LhsType a, RhsType b)
{
    if constexpr (std::is_signed<LhsType>())
        if (b == -1)
            return (ToValueType<LhsType>)0;

    if (b == 0)
        throw Trap();

    return (ToValueType<LhsType>)(a % b);
}

template <typename LhsType, typename RhsType>
constexpr Value operation_andnot(LhsType a, RhsType b)
{
    return a & ~b;
}

template <IsVector LhsType, typename RhsType>
constexpr Value operation_vector_shl(LhsType a, RhsType b)
{
    return a << (b % (sizeof(VectorElement<LhsType>) * 8));
}

template <IsVector LhsType, typename RhsType>
constexpr Value operation_vector_shr(LhsType a, RhsType b)
{
    return a >> (b % (sizeof(VectorElement<LhsType>) * 8));
}

template <IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_swizzle(LhsType a, RhsType b)
{
    // TODO: Use __builtin_shuffle on GCC
    LhsType result;
    for (size_t i = 0; i < lane_count<LhsType>(); i++)
    {
        if (b[i] < lane_count<LhsType>())
            result[i] = a[b[i]];
        else
            result[i] = 0;
    }
    return result;
}

template <IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_q15mulr_sat(LhsType a, RhsType b)
{
    static_assert(std::is_same<LhsType, int16x8_t>() && std::is_same<RhsType, int16x8_t>(), "Unsupported vector type for q15mulr_sat");

    // FIXME: This is a fancy way to do this simd

    // typedef int32_t __attribute__((vector_size(32))) int32x8_t;
    // typedef float32_t __attribute__((vector_size(32))) float32x8_t;
    // int32x8_t product = __builtin_convertvector(a, int32x8_t) * __builtin_convertvector(b, int32x8_t);

    // product += 0x4000; // Rounding addition for Q15 format
    // product >>= 15;

    // shifted = __builtin_convertvector(
    //     __builtin_convertvector(shifted, float32x8_t),
    //     int32x8_t);
    // shifted = vector_max(shifted, vector_broadcast<decltype(shifted)>(-32768));
    // shifted = vector_min(shifted, vector_broadcast<decltype(shifted)>(32767));

    // return __builtin_convertvector(shifted, int16x8_t);

    LhsType result;
    for (size_t i = 0; i < lane_count<LhsType>(); ++i)
    {
        int32_t product = static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
        product += 0x4000;
        product >>= 15;
        result[i] = saturate_to<int16_t>(product);
    }
    return result;
}

template <IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_dot(LhsType a, RhsType b)
{
    static_assert(std::is_same<LhsType, int16x8_t>() && std::is_same<RhsType, int16x8_t>(), "Unsupported vector type for dot");

    uint32x4_t result = {};
    for (size_t i = 0; i < lane_count<LhsType>(); i += 2)
        result[i / 2] = static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]) + static_cast<int32_t>(a[i + 1]) * static_cast<int32_t>(b[i + 1]);
    return result;
}

#define GENERIC_UNARY_OPERATION_FUNCTION(name, function) \
    template <typename T>                                \
    constexpr Value operation_##name(T a)                \
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
GENERIC_UNARY_OPERATION_FUNCTION(vector_sqrt, vector_sqrt);
GENERIC_UNARY_OPERATION_FUNCTION(vector_popcnt, vector_popcnt);

template <typename T>
constexpr Value operation_eqz(T a)
{
    return static_cast<uint32_t>(a == 0);
}
template <typename T>
constexpr Value operation_any_true(T a)
{
    return static_cast<uint32_t>(a != 0);
}

template <typename TruncatedType, typename T>
constexpr Value operation_trunc(T a)
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
constexpr Value operation_trunc_sat(T a)
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
constexpr Value operation_convert(T a)
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
constexpr Value operation_convert_s(T a)
{
    return operation_convert<ConvertedType, true>(a);
}

template <typename ConvertedType, typename T>
constexpr Value operation_convert_u(T a)
{
    return operation_convert<ConvertedType, false>(a);
}

template <typename ReinterpretedType, typename T>
constexpr Value operation_reinterpret(T a)
{
    return std::bit_cast<ReinterpretedType>(a);
}

template <typename LowType, typename T>
constexpr Value operation_extend(T a)
{
    static_assert(std::is_unsigned<LowType>(), "operation_extend expects low type to be unsigned");
    return static_cast<T>(static_cast<std::make_signed_t<T>>(static_cast<std::make_signed_t<LowType>>(static_cast<LowType>(a))));
}

template <IsVector VectorType, typename T>
constexpr Value operation_vector_broadcast(T a)
{
    return vector_broadcast<VectorType>(static_cast<VectorElement<VectorType>>(a));
}

template <IsVector T>
constexpr Value operation_vector_all_true(T a)
{
    for (size_t i = 0; i < lane_count<T>(); i++)
        if (a[i] == 0)
            return static_cast<uint32_t>(0);
    return static_cast<uint32_t>(1);
}

template <IsVector T>
constexpr Value operation_vector_bitmask(T a)
{
    uint32_t result = 0;
    for (size_t i = 0; i < lane_count<T>(); i++)
        if (a[i] < 0)
            result |= (1 << i);
    return result;
}

// TODO: Vector extends, narrows, promotes and demotes could be unified
template <IsVector ResultType, bool High, IsVector T>
constexpr Value operation_vector_extend(T a)
{
    static_assert(lane_count<ResultType>() == lane_count<T>() / 2);
    constexpr auto sourceOffset = High ? lane_count<T>() / 2 : 0;
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = a[i + sourceOffset];
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_extend_low(T a)
{
    return operation_vector_extend<ResultType, false>(a);
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_extend_high(T a)
{
    return operation_vector_extend<ResultType, true>(a);
}

template <IsVector ResultType, bool High, IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_extend_multiply(LhsType a, RhsType b)
{
    static_assert(sizeof(LhsType) == sizeof(RhsType));
    static_assert(lane_count<ResultType>() == lane_count<LhsType>() / 2);
    constexpr auto sourceOffset = High ? lane_count<LhsType>() / 2 : 0;
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = a[i + sourceOffset] * b[i + sourceOffset];
    return result;
}

template <IsVector ResultType, IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_extend_multiply_low(LhsType a, RhsType b)
{
    return operation_vector_extend_multiply<ResultType, false>(a, b);
}

template <IsVector ResultType, IsVector LhsType, IsVector RhsType>
constexpr Value operation_vector_extend_multiply_high(LhsType a, RhsType b)
{
    return operation_vector_extend_multiply<ResultType, true>(a, b);
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_narrow(T a, T b)
{
    static_assert(lane_count<ResultType>() / 2 == lane_count<T>());
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = saturate_to<VectorElement<ResultType>>(a[i]);
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i + lane_count<T>()] = saturate_to<VectorElement<ResultType>>(b[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_demote(T a)
{
    static_assert(lane_count<ResultType>() / 2 == lane_count<T>());
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = static_cast<VectorElement<ResultType>>(a[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_promote(T a)
{
    static_assert(lane_count<ResultType>() == lane_count<T>() / 2);
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = static_cast<VectorElement<ResultType>>(a[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_convert(T a)
{
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = static_cast<VectorElement<ResultType>>(a[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_trunc_sat(T a)
{
    static_assert(lane_count<ResultType>() >= lane_count<T>());
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = saturate_to<VectorElement<ResultType>>(std::trunc(a[i]));
    return result;
}
