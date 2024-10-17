#pragma once

#include <FileStream.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <sys/time.h>
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

inline void fill_buffer_with_random_data(uint8_t* data, size_t size)
{
#if LIBC_GLIBC_VERSION(2, 36)
    arc4random_buf(data, size);
#elif defined(OS_LINUX)
    FileStream randomStream("/dev/urandom");
    randomStream.read(data, size);
#else
    srand(time(nullptr));
    for (size_t i = 0; i < buffer_size; i++)
        data[i] = rand() % 256;
#endif
}

template <std::floating_point T>
T typed_nan()
{
    if constexpr (std::is_same<T, float>())
        return std::nanf("");
    else if constexpr (std::is_same<T, double>())
        return std::nan("");
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

using float32_t = float;
using float64_t = double;

using int128_t = __int128;
using uint128_t = unsigned __int128;
