#pragma once

#include <Stack.h>
#include <Trap.h>
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
            throw Trap(std::format("Unxpected type on the stack: {}, expected {}", get_type_name(value.get_type()), value_type_name<ToValueType<T>>));
#endif
        return std::bit_cast<T>(value.get<ToValueType<T>>());
    }
};
