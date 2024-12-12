#pragma once

#include <Type.h>
#include <Value.h>

class ValueStack
{
public:
    void push(Value value)
    {
        m_stack.push_back(value);
    }

    void push_values(std::vector<Value> values)
    {
        for (const auto& value : values)
            push(value);
    }

    Value pop()
    {
        if (size() == 0)
        {
            std::println(std::cerr, "Error: Tried to pop from an empty stack");
            throw Trap();
        }
        Value value = m_stack.back();
        m_stack.pop_back();
        return value;
    }

    template <IsValueType T>
    T pop_as()
    {
        Value value = pop();
        if (!value.holds_alternative<ToValueType<T>>())
        {
            std::println(std::cerr, "Error: Unxpected type on the stack: {}, expected {}", get_type_name(get_value_type(value)), value_type_name<ToValueType<T>>);
            throw Trap();
        }
        return std::bit_cast<T>(value.get<ToValueType<T>>());
    }

    std::vector<Value> pop_n_values(uint32_t n)
    {
        std::vector<Value> values;
        for (uint32_t i = 0; i < n; i++)
        {
            values.push_back(pop());
        }

        std::reverse(values.begin(), values.end());
        return values;
    }

    Value peek()
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

private:
    std::vector<Value> m_stack;
};
