#pragma once

#include <cmath>
#include <cstdint>

#ifdef ARCH_X86_64
    #include <immintrin.h>
    #include <smmintrin.h>
#endif

using int128_t = __int128;
using uint128_t = unsigned __int128;

using int8x2_t = int8_t __attribute__((vector_size(2)));
using int8x4_t = int8_t __attribute__((vector_size(4)));
using int8x8_t = int8_t __attribute__((vector_size(8)));
using int8x16_t = int8_t __attribute__((vector_size(16)));

using int16x2_t = int16_t __attribute__((vector_size(4)));
using int16x4_t = int16_t __attribute__((vector_size(8)));
using int16x8_t = int16_t __attribute__((vector_size(16)));

using int32x2_t = int32_t __attribute__((vector_size(8)));
using int32x4_t = int32_t __attribute__((vector_size(16)));
using int32x4_t = int32_t __attribute__((vector_size(16)));

using int64x2_t = int64_t __attribute__((vector_size(16)));

using uint8x2_t = uint8_t __attribute__((vector_size(2)));
using uint8x4_t = uint8_t __attribute__((vector_size(4)));
using uint8x8_t = uint8_t __attribute__((vector_size(8)));
using uint8x16_t = uint8_t __attribute__((vector_size(16)));

using uint16x2_t = uint16_t __attribute__((vector_size(4)));
using uint16x4_t = uint16_t __attribute__((vector_size(8)));
using uint16x8_t = uint16_t __attribute__((vector_size(16)));

using uint32x2_t = uint32_t __attribute__((vector_size(8)));
using uint32x4_t = uint32_t __attribute__((vector_size(16)));

using uint64x2_t = uint64_t __attribute__((vector_size(16)));

using float32x2_t = float __attribute__((vector_size(8)));
using float32x4_t = float __attribute__((vector_size(16)));

using float64x2_t = double __attribute__((vector_size(16)));

template <typename T>
concept IsVector = requires(T a) {
    { sizeof(T) == 16 };
    { a[0] };
    { a + 1 };
};

template <IsVector T>
using VectorElement = std::remove_cvref_t<decltype(std::declval<T>()[0])>;

template <IsVector T>
consteval size_t lane_count()
{
    return sizeof(T) / sizeof(VectorElement<T>);
}

template <IsVector T>
T vector_broadcast(VectorElement<T> value)
{
    T result = {};
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = value;
    return result;
}

#define GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(name, function)      \
    template <typename T, typename U>                                   \
        requires IsVector<T> && IsVector<U> && (sizeof(T) == sizeof(U)) \
    T vector_##name(T a, U b)                                           \
    {                                                                   \
        T result;                                                       \
        for (size_t i = 0; i < lane_count<T>(); i++)                    \
            result[i] = function(a[i], b[i]);                           \
        return result;                                                  \
    }

// FIXME: Use SIMD instructions for these operations
GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(add_sat, saturating_add)
GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(sub_sat, saturating_sub)
GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(min, nan_min)
GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(max, nan_max)

template <IsVector T>
T vector_pmin(T a, T b)
{
    return (b < a) ? b : a;
}

template <IsVector T>
T vector_pmax(T a, T b)
{
    return (a < b) ? b : a;
}

#define GENERIC_VECTOR_UNARY_INSTRUCTION_FUNCTION(name, function) \
    template <typename T>                                         \
        requires(IsVector<T>)                                     \
    T vector_##name(T vec)                                        \
    {                                                             \
        T result;                                                 \
        for (size_t i = 0; i < lane_count<T>(); i++)              \
            result[i] = function(vec[i]);                         \
        return result;                                            \
    }

template <IsVector T>
T vector_abs(T a)
{
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::abs(a[i]);
    return a;
}

template <IsVector T>
T vector_ceil(T a)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, float32x4_t>())
        return _mm_ceil_ps(a);
    else if constexpr (std::is_same<T, float64x2_t>())
        return _mm_ceil_pd(a);
    else
        static_assert(false, "Unsupported vector for ceil");
#else
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::ceil(a[i]);
    return a;
#endif
}

template <IsVector T>
T vector_floor(T a)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, float32x4_t>())
        return _mm_floor_ps(a);
    else if constexpr (std::is_same<T, float64x2_t>())
        return _mm_floor_pd(a);
    else
        static_assert(false, "Unsupported vector for floor");
#else
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::floor(a[i]);
    return a;
#endif
}

template <IsVector T>
T vector_trunc(T a)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, float32x4_t>())
        return _mm_round_ps(a, _MM_FROUND_TRUNC);
    else if constexpr (std::is_same<T, float64x2_t>())
        return _mm_round_pd(a, _MM_FROUND_TRUNC);
    else
        static_assert(false, "Unsupported vector for trunc");
#else
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::trunc(a[i]);
    return a;
#endif
}

template <IsVector T>
T vector_nearest(T a)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, float32x4_t>())
        return _mm_round_ps(a, _MM_FROUND_NINT);
    else if constexpr (std::is_same<T, float64x2_t>())
        return _mm_round_pd(a, _MM_FROUND_NINT);
    else
        static_assert(false, "Unsupported vector for nearest");
#else
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::nearbyint(a[i]);
    return a;
#endif
}

template <IsVector T>
T vector_sqrt(T a)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, float32x4_t>())
        return _mm_sqrt_ps(a);
    else if constexpr (std::is_same<T, float64x2_t>())
        return _mm_sqrt_pd(a);
    else
        static_assert(false, "Unsupported vector for sqrt");
#else
    for (size_t i = 0; i < lane_count<T>(); i++)
        a[i] = std::sqrt(a[i]);
    return a;
#endif
}

template <IsVector T>
T vector_avgr(T a, T b)
{
#ifdef ARCH_X86_64
    if constexpr (std::is_same<T, uint8x16_t>())
        return _mm_avg_epu8(a, b);
    else if constexpr (std::is_same<T, uint16x8_t>())
        return _mm_avg_epu16(a, b);
    else
        static_assert(false, "Unsupported vector for avgr");
#else
    T result;
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = (a[i] + b[i] + 1) / 2;
    return result;
#endif
}

template <IsVector T>
T vector_popcnt(T a)
{
#ifdef ARCH_X86_64_AVX512
    if constexpr (std::is_same<T, uint8x16_t>())
        return _mm_popcnt_epi8(a);
    else
        static_assert(false, "Unsupported vector for popcnt");
#else
    T result;
    for (size_t i = 0; i < lane_count<T>(); i++)
        result[i] = std::popcount(a[i]);
    return result;
#endif
}
