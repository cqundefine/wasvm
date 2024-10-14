#include <Type.h>
#include <WasmFile.h>
#include <cassert>
#include <cstdio>
#include <utility>

Type read_type_from_stream(Stream& stream)
{
    Type t = (Type)stream.read_leb<uint32_t>();
    if (!is_valid_type(t))
        throw WasmFile::InvalidWASMException();
    return t;
}

bool is_valid_type(Type t)
{
    return t == Type::i32 || t == Type::i64 || t == Type::f32 || t == Type::f64 || t == Type::v128 || t == Type::funcref || t == Type::externref;
}

Value default_value_for_type(Type type)
{
    switch (type)
    {
        case Type::i32:
            return (uint32_t)0;
        case Type::i64:
            return (uint64_t)0;
        case Type::f32:
            return (float)0.0f;
        case Type::f64:
            return (double)0.0;
        case Type::v128:
            return (uint128_t)0;
        case Type::funcref:
            return Reference { ReferenceType::Function, UINT32_MAX };
        case Type::externref:
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
    if (std::holds_alternative<uint128_t>(value))
        return Type::v128;
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

ReferenceType get_reference_type_from_reftype(Type type)
{
    if (type == Type::funcref)
        return ReferenceType::Function;
    if (type == Type::externref)
        return ReferenceType::Extern;

    fprintf(stderr, "Error: Unexpected ref type\n");
    throw Trap();
}

std::string get_type_name(Type type)
{
    switch (type)
    {
        case Type::i32:
            return "i32";
        case Type::i64:
            return "i64";
        case Type::f32:
            return "f32";
        case Type::f64:
            return "f64";
        case Type::v128:
            return "v128";
        case Type::funcref:
            return "funcref";
        case Type::externref:
            return "externref";
        default:
            assert(false);
    }

    std::unreachable();
}
