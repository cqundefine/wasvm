#pragma once

#include "Util/Util.h"
#include "VM/Trap.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

template <typename T, typename Exception = Trap>
class Stack
{
public:
    ALWAYS_INLINE Stack()
    {
        m_stack.reserve(32);
    }

    constexpr void push(T value)
    {
        m_stack.push_back(value);
    }

    constexpr void push_values(std::span<const T> values)
    {
        for (const auto& value : values)
            push(value);
    }

    [[nodiscard]] constexpr T pop()
    {
#ifdef DEBUG_BUILD
        if (size() == 0)
            throw Exception("Tried to pop from an empty stack");
#endif
        T value = m_stack.back();
        m_stack.pop_back();
        return value;
    }

    [[nodiscard]] constexpr std::vector<T> pop_n_values(uint32_t n)
    {
#ifdef DEBUG_BUILD
        if (size() < n)
            throw Exception("Not enough elements on the stack");
#endif
        std::vector<T> values(m_stack.data() + size() - n, m_stack.data() + size());
        erase(size() - n, 0);
        return std::move(values);
    }

    [[nodiscard]] constexpr std::span<T> span_last_n_values(uint32_t n)
    {
#ifdef DEBUG_BUILD
        if (size() < n)
            throw Exception("Not enough elements on the stack");
#endif
        std::span<T> values(m_stack.data() + size() - n, m_stack.data() + size());
        return values;
    }

    [[nodiscard]] constexpr const T& peek() const
    {
        return m_stack.back();
    }

    [[nodiscard]] constexpr T& peek()
    {
        return m_stack.back();
    }

    [[nodiscard]] constexpr T get_from_end(uint32_t index)
    {
#ifdef DEBUG_BUILD
        if (index + 1 > size())
            throw Exception("Error: Tried to get a stack element out of bounds");
#endif
        return m_stack[size() - index - 1];
    }

    constexpr void erase(uint32_t fromBegin, uint32_t fromEnd)
    {
        m_stack.erase(m_stack.begin() + fromBegin, m_stack.end() - fromEnd);
    }

    [[nodiscard]] constexpr uint32_t size() const
    {
        return static_cast<uint32_t>(m_stack.size());
    }

    [[nodiscard]] constexpr bool empty() const
    {
        return size() == 0;
    }

    constexpr void clear()
    {
        m_stack.clear();
    }

protected:
    std::vector<T> m_stack;
};
