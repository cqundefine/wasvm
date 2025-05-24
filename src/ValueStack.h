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
    T pop_as()
    {
        Value value = pop();
#ifdef DEBUG_BUILD
        if (!value.holds_alternative<ToValueType<T>>())
        {
            std::println(std::cerr, "Error: Unxpected type on the stack: {}, expected {}", get_type_name(get_value_type(value)), value_type_name<ToValueType<T>>);
            throw Trap();
        }
#endif
        return std::bit_cast<T>(value.get<ToValueType<T>>());
    }
};
