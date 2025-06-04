#pragma once

#include <Stack.h>
#include <Type.h>
#include <Value.h>
#include <iostream>
#include <span>

class ValueStack : public Stack<Value>
{
public:
    template <IsValueType T>
    constexpr T pop_as()
    {
        Value value = pop();
#ifdef DEBUG_BUILD
        if (!value.holds_alternative<ToValueType<T>>())
        {
            std::println(std::cerr, "Error: Unxpected type on the stack: {}, expected {}", get_type_name(value.get_type()), value_type_name<ToValueType<T>>);
            throw Trap();
        }
#endif
        return std::bit_cast<T>(value.get<ToValueType<T>>());
    }
};
