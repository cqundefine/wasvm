#pragma once

#include <Util.h>
#include <Value.h>
#include <span>

template <typename T, typename Exception = Trap>
class Stack
{
public:
    constexpr void push(T value)
    {
        m_stack.push_back(value);
    }

    constexpr void push_values(std::span<const T> values)
    {
        for (const auto& value : values)
            push(value);
    }

    constexpr T pop()
    {
#ifdef DEBUG_BUILD
        if (size() == 0)
        {
            std::println(std::cerr, "Error: Tried to pop from an empty stack");
            throw Exception();
        }
#endif
        T value = m_stack.back();
        m_stack.pop_back();
        return value;
    }

    constexpr std::vector<T> pop_n_values(uint32_t n)
    {
        std::vector<T> values;
        values.reserve(n);

        for (uint32_t i = 0; i < n; i++)
            values.push_back(pop());

        std::reverse(values.begin(), values.end());
        return std::move(values);
    }

    constexpr const T& peek() const
    {
        return m_stack.back();
    }

    constexpr T& peek()
    {
        return m_stack.back();
    }

    constexpr T get_from_end(uint32_t index)
    {
#ifdef DEBUG_BUILD
        if (index + 1 > size())
        {
            std::println(std::cerr, "Error: Tried to pop from an empty stack");
            throw Exception();
        }
#endif
        return m_stack[size() - index - 1];
    }

    constexpr void erase(uint32_t fromBegin, uint32_t fromEnd)
    {
        m_stack.erase(m_stack.begin() + fromBegin, m_stack.end() - fromEnd);
    }

    constexpr uint32_t size() const
    {
        return static_cast<uint32_t>(m_stack.size());
    }

    constexpr void clear()
    {
        m_stack.clear();
    }

protected:
    std::vector<T> m_stack;
};
