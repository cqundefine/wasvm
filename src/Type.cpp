#include <Type.h>
#include <cassert>
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
            return Reference { ReferenceType::Function, UINT32_MAX };
        case externref:
            return Reference { ReferenceType::Extern, UINT32_MAX };
        default:
            throw Trap();
    }
};

Type get_value_type(Value value)
{
    if (std::holds_alternative<uint32_t>(value))
        return Type::i32;
    if (std::holds_alternative<uint64_t>(value))
        return Type::i64;
    if (std::holds_alternative<float>(value))
        return Type::f32;
    if (std::holds_alternative<double>(value))
        return Type::f64;
    if (std::holds_alternative<Reference>(value))
    {
        if (std::get<Reference>(value).type == ReferenceType::Function)
            return Type::funcref;
        if (std::get<Reference>(value).type == ReferenceType::Extern)
            return Type::externref;
        assert(false);
    }

    if (std::holds_alternative<Label>(value))
    {
        fprintf(stderr, "Error: Tried to get type for a label value\n");
        throw Trap();
    }

    fprintf(stderr, "Error: Unexpected value type\n");
    throw Trap();
}

ReferenceType get_reference_type_from_reftype(uint8_t type)
{
    if (type == Type::funcref)
        return ReferenceType::Function;
    if (type == Type::externref)
        return ReferenceType::Extern;

    fprintf(stderr, "Error: Unexpected ref type\n");
    throw Trap();
}
