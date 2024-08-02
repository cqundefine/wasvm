#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <variant>
#include <vector>

struct Trap
{
};

enum class LabelBeginType
{
    Else,
    Other,
    LoopInvalid
};

struct Label
{
    uint32_t continuation;
    uint32_t arity;
    LabelBeginType beginType;
};

enum class ReferenceType
{
    Function,
    Extern
};

struct Reference
{
    ReferenceType type;
    uint32_t index;

    Reference(ReferenceType type, uint32_t index)
        : type(type)
        , index(index)
    {
    }
};
typedef std::variant<uint32_t, uint64_t, float, double, Reference, Label> Value;

const char* get_value_variant_name_by_index(size_t index);

template <typename T>
extern const char* value_type_name;

template <>
extern const char* value_type_name<uint32_t>;

template <>
extern const char* value_type_name<uint64_t>;

template <>
extern const char* value_type_name<float>;

template <>
extern const char* value_type_name<double>;

template <>
extern const char* value_type_name<Reference>;

template <>
extern const char* value_type_name<Label>;

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename T>
using __ToValueType = typename std::conditional<std::is_integral_v<T>, std::make_unsigned<T>, std::common_type<T>>::type;

template <typename T>
using ToValueType = typename __ToValueType<T>::type;

template <typename T>
concept IsValueType = IsAnyOf<ToValueType<T>, uint32_t, uint64_t, float, double, Reference, Label>;

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
            printf("Error: Unxpected type on the stack: %s, expected %s\n", get_value_variant_name_by_index(value.index()), value_type_name<ToValueType<T>>);
            throw Trap();
        }
        return static_cast<T>(std::get<ToValueType<T>>(value));
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
