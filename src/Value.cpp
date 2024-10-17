#include <Type.h>
#include <Value.h>
#include <cassert>
#include <format>

template <typename T>
const char* value_type_name = "invalid";

template <>
const char* value_type_name<uint32_t> = "i32";

template <>
const char* value_type_name<uint64_t> = "i64";

template <>
const char* value_type_name<float> = "f32";

template <>
const char* value_type_name<double> = "f64";

template <>
const char* value_type_name<uint128_t> = "v128";

template <>
const char* value_type_name<Reference> = "funcref/externref";

template <>
const char* value_type_name<Label> = "label";

std::string value_to_string(Value value)
{
    if (value.holds_alternative<uint32_t>())
        return std::format("{}({})", get_type_name(get_value_type(value)), value.get<uint32_t>());
    if (value.holds_alternative<uint64_t>())
        return std::format("{}({})", get_type_name(get_value_type(value)), value.get<uint64_t>());
    if (value.holds_alternative<float>())
        return std::format("{}({})", get_type_name(get_value_type(value)), value.get<float>());
    if (value.holds_alternative<double>())
        return std::format("{}({})", get_type_name(get_value_type(value)), value.get<double>());
    if (value.holds_alternative<uint128_t>())
        return std::format("{}({})", get_type_name(get_value_type(value)), value.get<uint128_t>());
    if (value.holds_alternative<Reference>())
    {
        uint32_t index = value.get<Reference>().index;
        if (index == UINT32_MAX)
            return std::format("{}(null)", get_type_name(get_value_type(value)));
        else
            return std::format("{}({})", get_type_name(get_value_type(value)), index);
    }
    assert(false);
}
