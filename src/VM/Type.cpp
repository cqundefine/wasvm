#include "Type.h"
#include "Value.h"
#include "WasmFile/WasmFile.h"
#include <utility>

Type read_type_from_stream(Stream& stream)
{
    Type type = static_cast<Type>(stream.read_leb<uint32_t>());
    if (!is_valid_type(type))
        throw WasmFile::InvalidWASMException("Invalid type");
    return type;
}

bool is_valid_type(Type type)
{
    return type == Type::i32 || type == Type::i64 || type == Type::f32 || type == Type::f64 || type == Type::v128 || type == Type::funcref || type == Type::externref;
}

Value default_value_for_type(Type type)
{
    switch (type)
    {
        case Type::i32:
            return uint32_t { 0 };
        case Type::i64:
            return uint64_t { 0 };
        case Type::f32:
            return float { 0.0f };
        case Type::f64:
            return double { 0.0 };
        case Type::v128:
            return uint128_t { 0 };
        case Type::funcref:
            return Reference { ReferenceType::Function, {}, nullptr };
        case Type::externref:
            return Reference { ReferenceType::Extern, {}, nullptr };
        default:
            throw Trap("Invalid type");
    }
};

ReferenceType get_reference_type_from_reftype(Type type)
{
    if (type == Type::funcref)
        return ReferenceType::Function;
    if (type == Type::externref)
        return ReferenceType::Extern;

    throw Trap("Unexpected ref type");
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
            throw Trap("Invalid type");
    }
}

bool is_reference_type(Type type)
{
    return type == Type::externref || type == Type::funcref;
}

Type type_from_address_type(AddressType addressType)
{
    switch (addressType)
    {
        using enum AddressType;
        case i32:
            return Type::i32;
        case i64:
            return Type::i64;
        default:
            std::unreachable();
    }
}
