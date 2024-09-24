#include <Util.h>

template <typename T>
const char* value_type_name = "invalid";

template <>
const char* value_type_name<float> = "f32";

template <>
const char* value_type_name<double> = "f64";
