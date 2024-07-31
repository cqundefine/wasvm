#pragma once

#include <cstdint>
#include <map>
#include <vector>

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
