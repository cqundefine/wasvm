#pragma once

#include <Util.h>

using int8x2_t = int8_t __attribute__((vector_size(2)));
using int8x4_t = int8_t __attribute__((vector_size(4)));
using int8x8_t = int8_t __attribute__((vector_size(8)));
using int8x16_t = int8_t __attribute__((vector_size(16)));

using int16x2_t = int16_t __attribute__((vector_size(4)));
using int16x4_t = int16_t __attribute__((vector_size(8)));
using int16x8_t = int16_t __attribute__((vector_size(16)));

using int32x2_t = int32_t __attribute__((vector_size(8)));
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

using float32x2_t = float32_t __attribute__((vector_size(8)));
using float32x4_t = float32_t __attribute__((vector_size(16)));

using float64x2_t = float64_t __attribute__((vector_size(16)));

template <typename T>
concept IsVector = requires(T a) {
    { sizeof(T) == 16 };
    { a[0] };
};

template <typename T>
using VectorElement = decltype(std::declval<T>()[0]);

#define GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(name, function)        \
    template <typename T>                                                 \
        requires(IsVector<T>)                                             \
    T vector_##name(T a, T b)                                             \
    {                                                                     \
        T result;                                                         \
        for (size_t i = 0; i < sizeof(T) / sizeof(VectorElement<T>); ++i) \
            result[i] = function(a[i], b[i]);                             \
        return result;                                                    \
    }

GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(min, std::min);
GENERIC_VECTOR_BINARY_INSTRUCTION_FUNCTION(max, std::max);

#define GENERIC_VECTOR_UNARY_INSTRUCTION_FUNCTION(name, function)         \
    template <typename T>                                                 \
        requires(IsVector<T>)                                             \
    T vector_##name(T vec)                                                \
    {                                                                     \
        T result;                                                         \
        for (size_t i = 0; i < sizeof(T) / sizeof(VectorElement<T>); ++i) \
            result[i] = function(vec[i]);                                 \
        return result;                                                    \
    }

GENERIC_VECTOR_UNARY_INSTRUCTION_FUNCTION(abs, std::abs);
