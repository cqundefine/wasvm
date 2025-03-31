#pragma once

#include <algorithm>
#include <bits/floatn-common.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sys/time.h>
#include <type_traits>
#include <vector>

#if defined(__linux__)
    #define OS_LINUX
#endif

#if defined(__GLIBC__)
    #define LIBC_GLIBC
    #define LIBC_GLIBC_VERSION(maj, min) __GLIBC_PREREQ((maj), (min))
#else
    #define LIBC_GLIBC_VERSION(maj, min) 0
#endif

#if !defined(NDEBUG)
    #define DEBUG_BUILD
#endif

#define OFFSET_OF(class, member) (reinterpret_cast<ptrdiff_t>(&reinterpret_cast<class*>(0x1000)->member) - 0x1000)

template <typename T>
using Own = std::unique_ptr<T>;
template <typename T, typename... Args>
constexpr Own<T> MakeOwn(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
using Ref = std::shared_ptr<T>;
template <typename T, typename... Args>
constexpr Ref<T> MakeRef(Args&&... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T, typename Base>
constexpr Ref<T> StaticRefCast(const Ref<Base>& base)
{
    return std::static_pointer_cast<T>(base);
}

void fill_buffer_with_random_data(uint8_t* data, size_t size);

template <std::floating_point T>
consteval T typed_nan()
{
    if constexpr (std::is_same<T, float>())
        return __builtin_nanf32("");
    else if constexpr (std::is_same<T, double>())
        return __builtin_nanf64("");
    else
        static_assert(false, "Unsupported type of NaN");
}

template <typename T>
bool vector_contains(const std::vector<T>& v, T x)
{
    if (std::find(v.begin(), v.end(), x) != v.end())
        return true;
    return false;
}

bool is_valid_utf8(const std::string& string);

template <typename T, typename U>
    requires std::is_arithmetic_v<T> && std::is_arithmetic_v<U>
constexpr T ceil_div(T a, U b)
{
    T result = a / b;
    if ((a % b) != 0 && (a > 0) == (b > 0))
        result++;
    return result;
}

template <typename T, typename U>
    requires std::is_integral_v<T> && std::is_integral_v<U>
constexpr T saturating_add(T a, U b)
{
    if (a > 0 && b > 0 && a > std::numeric_limits<T>::max() - b)
        return std::numeric_limits<T>::max();
    if (a < 0 && b < 0 && a < std::numeric_limits<T>::min() - b)
        return std::numeric_limits<T>::min();
    return a + b;
}

template <typename T, typename U>
    requires std::is_integral_v<T> && std::is_integral_v<U>
constexpr T saturating_sub(T a, U b)
{
    if (a > 0 && b < 0 && a > std::numeric_limits<T>::max() + b)
        return std::numeric_limits<T>::max();
    if (a < 0 && b > 0 && a < std::numeric_limits<T>::min() + b)
        return std::numeric_limits<T>::min();
    return a - b;
}

template <typename T>
constexpr T nan_min(T a, T b)
{
    if constexpr (std::is_floating_point<T>())
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
    return (a < b) ? a : b;
}

template <typename T>
constexpr T nan_max(T a, T b)
{
    if constexpr (std::is_floating_point<T>())
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();
    return (a > b) ? a : b;
}

using float32_t = float;
using float64_t = double;

using int128_t = __int128;
using uint128_t = unsigned __int128;
