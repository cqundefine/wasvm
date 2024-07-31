#include <Value.h>
#include <cassert>

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
const char* value_type_name<FunctionRefrence> = "funcref";

template <>
const char* value_type_name<Label> = "label";

const char* get_value_variant_name_by_index(size_t index)
{
    switch (index)
    {
        case 0:
            return "i32";
        case 1:
            return "i64";
        case 2:
            return "f32";
        case 3:
            return "f64";
        case 4:
            return "funcref";
        case 5:
            return "label";
        default:
            assert(false);
    }

    return "?????";
}
