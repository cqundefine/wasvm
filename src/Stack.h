#pragma once

#include <Util.h>
#include <Value.h>

template <typename T, typename Exception = Trap>
class Stack
{
public:
    void push(T value)
    {
        m_stack.push_back(value);
    }

    void push_values(std::span<T> values)
    {
        for (const auto& value : values)
            push(value);
    }

    T pop()
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

    std::vector<T> pop_n_values(uint32_t n)
    {
        std::vector<T> values;
        for (uint32_t i = 0; i < n; i++)
            values.push_back(pop());

        std::reverse(values.begin(), values.end());
        return std::move(values);
    }

    T peek()
    {
        return m_stack.back();
    }

    void erase(uint32_t fromBegin, uint32_t fromEnd)
    {
        m_stack.erase(m_stack.begin() + fromBegin, m_stack.end() - fromEnd);
    }

    uint32_t size() const
    {
        return static_cast<uint32_t>(m_stack.size());
    }

    void clear()
    {
        m_stack.clear();
    }

protected:
    std::vector<T> m_stack;
};
