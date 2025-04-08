#include "Util.h"
#include <Type.h>
#include <Value.h>
#include <cassert>
#include <format>
#include <utility>

static bool float_equals(float a, float b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    return a == b;
}

static bool double_equals(double a, double b)
{
    if (std::isnan(a) && std::isnan(b))
        return true;
    return a == b;
}

bool Value::operator==(const Value& other) const
{
    if (m_type != other.m_type)
        return false;

    if (holds_alternative<uint32_t>())
        return get<uint32_t>() == other.get<uint32_t>();
    if (holds_alternative<uint64_t>())
        return get<uint64_t>() == other.get<uint64_t>();

    if (holds_alternative<float>())
        return float_equals(get<float>(), other.get<float>());
    if (holds_alternative<double>())
        return double_equals(get<double>(), other.get<double>());

    if (holds_alternative<uint128_t>())
        return get<uint128_t>() == other.get<uint128_t>();

    if (holds_alternative<Reference>())
    {
        auto refA = get<Reference>();
        auto refB = other.get<Reference>();

        if (refA.type != refB.type)
            return false;

        return refA.index == refB.index;
    }

    UNREACHABLE();
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
