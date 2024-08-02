#pragma once

#include <cstdint>
#include <FileStream.h>
#include <sys/time.h>
#include <map>
#include <vector>

#if defined(__linux__)
#    define OS_LINUX
#endif

#if defined(__GLIBC__)
#    define LIBC_GLIBC
#    define LIBC_GLIBC_VERSION(maj, min) __GLIBC_PREREQ((maj), (min))
#else
#    define LIBC_GLIBC_VERSION(maj, min) 0
#endif

inline void fill_buffer_with_random_data(uint8_t* data, size_t size)
{
#if LIBC_GLIBC_VERSION(2, 36)
    arc4random_buf(data, size);
#elif defined(OS_LINUX)
    FileStream randomStream("/dev/urandom");
    randomStream.read(data, size);
#else
    srand(time(nullptr));
    for(size_t i = 0; i < buffer_size; i++)
        data[i] = rand() % 256;
#endif
}

template <typename T>
std::map<uint32_t, T> vector_to_map_offset(const std::vector<T>& vector, uint32_t offset)
{
    std::map<uint32_t, T> map;
    for (size_t i = 0; i < vector.size(); i++)
    {
        map[i + offset] = vector.at(i);
    }
    return map;
}
