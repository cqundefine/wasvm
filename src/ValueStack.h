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
            fprintf(stderr, "Error: Tried to pop from an empty stack\n");
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
        if (!std::holds_alternative<ToValueType<T>>(value))
        {
            printf("Error: Unxpected type on the stack: %s, expected %s\n", get_type_name(get_value_type(value)).c_str(), value_type_name<ToValueType<T>>);
            throw Trap();
        }
        return std::bit_cast<T>(std::get<ToValueType<T>>(value));
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

    Label nth_label(uint32_t n)
    {
        n++;
        for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it)
        {
            if (std::holds_alternative<Label>(*it))
            {
                if (--n == 0)
                {
                    return std::get<Label>(*it);
                }
            }
        }

        fprintf(stderr, "Error: Not enough labels on the stack\n");
        throw Trap();
    }

    size_t size() const
    {
        return m_stack.size();
    }

    void clear()
    {
        m_stack.clear();
    }

private:
    std::vector<Value> m_stack;
};
