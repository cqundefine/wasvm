#pragma once

#include "Util/SIMD.h"
#include "Util/Util.h"
#include "Value.h"
#include <cmath>
#include <concepts>

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
        return static_cast<uint32_t>(a op b);              \
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

template <std::floating_point LhsType, std::floating_point RhsType>
constexpr Value operation_min(LhsType a, RhsType b)
{
    if (std::isnan(a))
        return typed_nan<ToValueType<LhsType>>();

    if (std::isnan(b))
        return typed_nan<ToValueType<LhsType>>();

    return (ToValueType<LhsType>)std::min(a, b);
}

template <std::floating_point LhsType, std::floating_point RhsType>
constexpr Value operation_max(LhsType a, RhsType b)
{
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
            if (a == (static_cast<ToValueType<LhsType>>(1) << (sizeof(LhsType) * 8 - 1)) && b == -1)
                throw Trap("Division overflow");

        if (b == 0)
            throw Trap("Division by zero");
    }

    return (ToValueType<LhsType>)(a / b);
}

template <typename LhsType, typename RhsType>
constexpr Value operation_rem(LhsType a, RhsType b)
{
    if constexpr (std::is_signed<LhsType>())
        if (b == -1)
            return static_cast<ToValueType<LhsType>>(0);

    if (b == 0)
        throw Trap("Division by zero");

    return static_cast<ToValueType<LhsType>>(a % b);
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
    LhsType result {};
    for (size_t i = 0; i < lane_count<LhsType>(); i++)
    {
        if (b[i] < lane_count<LhsType>())
            result[i] = a[b[i]];
        else
            result[i] = 0;
    }
    return result;
}

constexpr Value operation_vector_q15mulr_sat(int16x8_t a, int16x8_t b)
{
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

    int16x8_t result {};
    for (size_t i = 0; i < lane_count<int16x8_t>(); ++i)
    {
        int32_t product = static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
        product += 0x4000;
        product >>= 15;
        result[i] = saturate_to<int16_t>(product);
    }
    return result;
}

constexpr Value operation_vector_dot(int16x8_t a, int16x8_t b)
{
    uint32x4_t result {};
    for (size_t i = 0; i < lane_count<int16x8_t>(); i += 2)
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

template <std::integral TruncatedType, std::floating_point T>
constexpr Value operation_trunc(T a)
{
    if (std::isnan(a) || std::isinf(a))
        throw Trap("NaN or Inf in truncate");

    a = std::trunc(a);

    constexpr auto minimum = static_cast<long double>(std::numeric_limits<TruncatedType>::min());
    constexpr auto maximum = static_cast<long double>(std::numeric_limits<TruncatedType>::max());

    if (static_cast<long double>(a) < minimum)
        throw Trap("Truncate overflow");

    if (static_cast<long double>(a) > maximum)
        throw Trap("Truncate overflow");

    return static_cast<ToValueType<TruncatedType>>(static_cast<TruncatedType>(a));
}

template <std::integral TruncatedType, std::floating_point T>
constexpr Value operation_trunc_sat(T a)
{
    if (std::isnan(a))
        return static_cast<ToValueType<TruncatedType>>(0);

    if (std::isinf(a) && a < 0)
        return static_cast<ToValueType<TruncatedType>>(std::numeric_limits<TruncatedType>::min());

    if (std::isinf(a) && a > 0)
        return static_cast<ToValueType<TruncatedType>>(std::numeric_limits<TruncatedType>::max());

    a = std::trunc(a);

    constexpr auto minimum = static_cast<long double>(std::numeric_limits<TruncatedType>::min());
    constexpr auto maximum = static_cast<long double>(std::numeric_limits<TruncatedType>::max());

    if (static_cast<long double>(a) < minimum)
        return static_cast<ToValueType<TruncatedType>>(std::numeric_limits<TruncatedType>::min());

    if (static_cast<long double>(a) > maximum)
        return static_cast<ToValueType<TruncatedType>>(std::numeric_limits<TruncatedType>::max());

    return static_cast<ToValueType<TruncatedType>>(static_cast<TruncatedType>(a));
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

template <std::unsigned_integral LowType, std::integral T>
constexpr Value operation_extend(T a)
{
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
    requires(lane_count<ResultType>() == lane_count<T>() / 2)
constexpr Value operation_vector_extend(T a)
{
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
    requires(sizeof(LhsType) == sizeof(RhsType)) && (lane_count<ResultType>() == lane_count<LhsType>() / 2)
constexpr Value operation_vector_extend_multiply(LhsType a, RhsType b)
{
    constexpr auto sourceOffset = High ? lane_count<LhsType>() / 2 : 0;
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = static_cast<VectorElement<ResultType>>(a[i + sourceOffset]) * static_cast<VectorElement<ResultType>>(b[i + sourceOffset]);
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
    requires(lane_count<ResultType>() / 2 == lane_count<T>())
constexpr Value operation_vector_narrow(T a, T b)
{
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = saturate_to<VectorElement<ResultType>>(a[i]);
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i + lane_count<T>()] = saturate_to<VectorElement<ResultType>>(b[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
    requires(lane_count<ResultType>() / 2 == lane_count<T>())
constexpr Value operation_vector_demote(T a)
{
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = static_cast<VectorElement<ResultType>>(a[i]);
    return result;
}

template <IsVector ResultType, IsVector T>
    requires(lane_count<ResultType>() == lane_count<T>() / 2)
constexpr Value operation_vector_promote(T a)
{
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
    requires(lane_count<ResultType>() >= lane_count<T>())
constexpr Value operation_vector_trunc_sat(T a)
{
    ResultType result {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = saturate_to<VectorElement<ResultType>>(std::trunc(a[i]));
    return result;
}

template <IsVector ResultType, IsVector T>
constexpr Value operation_vector_extadd_pairwise(T a)
{
    ResultType result {};
    for (size_t i = 0; i < lane_count<ResultType>(); i++)
        result[i] = a[i * 2] + a[i * 2 + 1];
    return result;
}
