#include <Type.h>
#include <WasmFile.h>
#include <cassert>
#include <cstdio>
#include <iostream>
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
    if (value.holds_alternative<uint32_t>())
        return Type::i32;
    if (value.holds_alternative<uint64_t>())
        return Type::i64;
    if (value.holds_alternative<float>())
        return Type::f32;
    if (value.holds_alternative<double>())
        return Type::f64;
    if (value.holds_alternative<uint128_t>())
        return Type::v128;
    if (value.holds_alternative<Reference>())
    {
        if (value.get<Reference>().type == ReferenceType::Function)
            return Type::funcref;
        if (value.get<Reference>().type == ReferenceType::Extern)
            return Type::externref;
        assert(false);
    }

    std::println(std::cerr, "Error: Unexpected value type");
    throw Trap();
}

ReferenceType get_reference_type_from_reftype(Type type)
{
    if (type == Type::funcref)
        return ReferenceType::Function;
    if (type == Type::externref)
        return ReferenceType::Extern;

    std::println(std::cerr, "Error: Unexpected ref type");
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

Value::Type value_type_from_type(Type type)
{
    switch (type)
    {
        case Type::i32:
            return Value::Type::UInt32;
        case Type::i64:
            return Value::Type::UInt64;
        case Type::f32:
            return Value::Type::Float;
        case Type::f64:
            return Value::Type::Double;
        case Type::v128:
            return Value::Type::UInt128;
        case Type::funcref:
        case Type::externref:
            return Value::Type::Reference;
        default:
            assert(false);
    }

    std::unreachable();
}

bool is_reference_type(Type type)
{
    return type == Type::externref || type == Type::funcref;
}
