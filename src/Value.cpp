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
    if (std::holds_alternative<uint32_t>(value))
        return std::format("{}({})", get_type_name(get_value_type(value)), std::get<uint32_t>(value));
    if (std::holds_alternative<uint64_t>(value))
        return std::format("{}({})", get_type_name(get_value_type(value)), std::get<uint64_t>(value));
    if (std::holds_alternative<float>(value))
        return std::format("{}({})", get_type_name(get_value_type(value)), std::get<float>(value));
    if (std::holds_alternative<double>(value))
        return std::format("{}({})", get_type_name(get_value_type(value)), std::get<double>(value));
    if (std::holds_alternative<uint128_t>(value))
        return std::format("{}({})", get_type_name(get_value_type(value)), std::get<uint128_t>(value));
    if (std::holds_alternative<Reference>(value))
    {
        uint32_t index = std::get<Reference>(value).index;
        if (index == UINT32_MAX)
            return std::format("{}(null)", get_type_name(get_value_type(value)));
        else
            return std::format("{}({})", get_type_name(get_value_type(value)), index);
    }
    assert(false);
}
