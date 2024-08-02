#pragma once

#include <bit>
#include <cassert>
#include <cmath>
#include <Value.h>

#define GENERIC_BINARY_OPERATION_OPERATOR(name, op) \
    template <typename LhsType, typename RhsType>   \
    Value operation_##name(LhsType a, RhsType b)    \
    {                                               \
        return (ToValueType<LhsType>)(a op b);      \
    }

#define GENERIC_BINARY_OPERATION_FUNCTION(name, function) \
    template <typename LhsType, typename RhsType>         \
    Value operation_##name(LhsType a, RhsType b)          \
    {                                                     \
        return (ToValueType<LhsType>)function(a, b);      \
    }

#define GENERIC_COMPARISON_OPERATION_OPERATOR(name, op) \
    template <typename LhsType, typename RhsType>       \
    Value operation_##name(LhsType a, RhsType b)        \
    {                                                   \
        return (uint32_t)(a op b);                      \
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
GENERIC_BINARY_OPERATION_FUNCTION(min, std::min);
GENERIC_BINARY_OPERATION_FUNCTION(max, std::max);
GENERIC_BINARY_OPERATION_FUNCTION(copysign, std::copysign);
GENERIC_COMPARISON_OPERATION_OPERATOR(eq, ==);
GENERIC_COMPARISON_OPERATION_OPERATOR(ne, !=);
GENERIC_COMPARISON_OPERATION_OPERATOR(lt, <);
GENERIC_COMPARISON_OPERATION_OPERATOR(gt, >);
GENERIC_COMPARISON_OPERATION_OPERATOR(le, <=);
GENERIC_COMPARISON_OPERATION_OPERATOR(ge, >=);

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
GENERIC_UNARY_OPERATION_FUNCTION(ceil, std::ceil);
GENERIC_UNARY_OPERATION_FUNCTION(floor, std::floor);
GENERIC_UNARY_OPERATION_FUNCTION(trunc, std::trunc);
GENERIC_UNARY_OPERATION_FUNCTION(nearest, std::nearbyint);
GENERIC_UNARY_OPERATION_FUNCTION(sqrt, std::sqrt);

template <typename T>
Value operation_eqz(T a)
{
    return (uint32_t)(a == 0);
}

template <typename TruncatedType, typename T>
Value operation_trunc(T a)
{
    static_assert(std::is_floating_point<T>() && std::is_integral<TruncatedType>(), "run_truncate_instruction is meant for floating point to integer conversions");
    
    if (std::isnan(a) || std::isinf(a))
        throw Trap();

    a = std::trunc(a);

    // FIXME: Check the limits

    return (ToValueType<TruncatedType>)(TruncatedType)a;
}
