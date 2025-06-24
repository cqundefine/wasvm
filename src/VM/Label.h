#pragma once

#include <cstdint>

struct Label
{
    uint32_t continuation;
    uint32_t arity;
    uint32_t stackHeight;
};
