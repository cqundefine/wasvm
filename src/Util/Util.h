#pragma once

#include <cmath>
#include <memory>
#include <vector>

#if defined(__linux__)
    #define OS_LINUX
#endif

#if defined(__x86_64__)
    #define ARCH_X86_64
#endif

#if defined(__GLIBC__)
    #define LIBC_GLIBC
    #define LIBC_GLIBC_VERSION(maj, min) __GLIBC_PREREQ((maj), (min))
#else
    #define LIBC_GLIBC_VERSION(maj, min) 0
#endif

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
constexpr Ref<T> as(const Ref<Base>& base)
{
    return std::static_pointer_cast<T>(base);
}

template <typename T>
using Weak = std::weak_ptr<T>;

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

void fill_buffer_with_random_data(uint8_t* data, size_t size);

using defer = std::shared_ptr<void>;
#define DEFER(x) defer _(nullptr, [](...) { x; });

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
    return std::find(v.begin(), v.end(), x) != v.end();
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

template <typename Result, typename T>
constexpr Result saturate_to(T a)
{
    using Convertee = std::conditional_t<std::is_floating_point_v<T>, double, T>;

    if constexpr (std::is_integral<Result>() && std::is_floating_point<T>())
    {
        if (std::isnan(a))
            return 0;
    }

    constexpr auto min = static_cast<Convertee>(std::numeric_limits<Result>::min());
    constexpr auto max = static_cast<Convertee>(std::numeric_limits<Result>::max());

    auto clamped = std::clamp(static_cast<Convertee>(a), min, max);
    return static_cast<Result>(clamped);
}

template <std::integral T>
constexpr T saturating_add(T a, T b)
{
    int64_t result = static_cast<int64_t>(a) + static_cast<int64_t>(b);
    return saturate_to<T>(result);
}

template <std::integral T>
constexpr T saturating_sub(T a, T b)
{
    int64_t result = static_cast<int64_t>(a) - static_cast<int64_t>(b);
    return saturate_to<T>(result);
}

template <typename T>
constexpr T nan_min(T a, T b)
{
    if constexpr (std::is_floating_point<T>())
    {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();

        if (a == 0 && b == 0)
            return std::signbit(a) ? a : b;
    }

    return (a < b) ? a : b;
}

template <typename T>
constexpr T nan_max(T a, T b)
{
    if constexpr (std::is_floating_point<T>())
    {
        if (std::isnan(a) || std::isnan(b))
            return std::numeric_limits<T>::quiet_NaN();

        if (a == 0 && b == 0)
            return std::signbit(a) ? b : a;
    }
    return (a > b) ? a : b;
}
