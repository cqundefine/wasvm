#include <Type.h>
#include <cstdio>

bool is_valid_type(uint8_t t)
{
    return t == i32 || t == i64 || t == f32 || t == f64 || t == funcref || t == externref;
}

Value default_value_for_type(uint8_t type)
{
    switch (type)
    {
        case i32:
            return (uint32_t)0;
        case i64:
            return (uint64_t)0;
        case f32:
            return (float)0.0f;
        case f64:
            return (double)0.0;
        case funcref:
            return (FunctionRefrence) { UINT32_MAX };
        default:
            throw Trap();
    }
};

Type get_value_type(Value value)
{
    if (std::holds_alternative<uint32_t>(value))
        return i32;
    if (std::holds_alternative<uint64_t>(value))
        return i64;
    if (std::holds_alternative<float>(value))
        return f32;
    if (std::holds_alternative<double>(value))
        return f64;
    if (std::holds_alternative<FunctionRefrence>(value))
        return funcref;

    if (std::holds_alternative<Label>(value))
    {
        fprintf(stderr, "Error: Tried to get type for a label value\n");
        throw Trap();
    }

    fprintf(stderr, "Error: Unexpected value type\n");
    throw Trap();
}
