#pragma once

#include <SIMD.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

struct Trap
{
};

struct Label
{
    uint32_t continuation;
    uint32_t arity;
    uint32_t stackHeight;
};

enum class ReferenceType
{
    Function,
    Extern
};

struct Reference
{
    ReferenceType type;
    uint32_t index;

    constexpr Reference(ReferenceType type, uint32_t index)
        : type(type)
        , index(index)
    {
    }
};
static_assert(sizeof(Reference) == 8);

template <typename T>
extern const char* value_type_name;

template <>
extern const char* value_type_name<uint32_t>;

template <>
extern const char* value_type_name<uint64_t>;

template <>
extern const char* value_type_name<float>;

template <>
extern const char* value_type_name<double>;

template <>
extern const char* value_type_name<uint128_t>;

template <>
extern const char* value_type_name<Reference>;

template <typename T, typename = void>
struct ToValueTypeHelper
{
    using type = T;
};

template <typename T>
struct ToValueTypeHelper<T, std::enable_if_t<std::is_integral_v<T> && sizeof(T) <= 8>>
{
    using type = std::make_unsigned_t<T>;
};

template <typename T>
    requires IsVector<T>
struct ToValueTypeHelper<T>
{
    using type = uint128_t;
};

template <typename T>
using ToValueType = typename ToValueTypeHelper<T>::type;

static_assert(std::is_same<ToValueType<uint32_t>, uint32_t>());
static_assert(std::is_same<ToValueType<int32_t>, uint32_t>());
static_assert(std::is_same<ToValueType<float>, float>());
static_assert(std::is_same<ToValueType<Reference>, Reference>());
static_assert(std::is_same<ToValueType<uint128_t>, uint128_t>());
static_assert(std::is_same<ToValueType<int64x2_t>, uint128_t>());

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <typename T>
concept IsValueType = IsAnyOf<ToValueType<T>, uint32_t, uint64_t, float, double, uint128_t, Reference>;

class Value
{
    friend class ValueStack;

public:
    // Enum class for types
    enum class Type : uint64_t
    {
        UInt32,
        UInt64,
        Float,
        Double,
        UInt128,
        Reference
    };
    static_assert(sizeof(Type) == sizeof(uint64_t));

    // Default constructor
    constexpr Value()
        : m_type(Type::UInt32)
    {
        new (&m_data) uint32_t(0); // Initialize with default value
    }

    // Constructor that uses the IsValueType concept
    template <IsValueType T>
    constexpr Value(const T& value)
        : m_type(get_type_for<ToValueType<T>>())
    {
        new (&m_data) ToValueType<T>((ToValueType<T>)value);
    }

    // Default copy and move constructors/assignments
    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;

    // Templated check if the variant holds a specific type
    template <IsValueType T>
    constexpr bool holds_alternative() const
    {
        return m_type == get_type_for<T>();
    }

    // Access the stored value as a reference (throws if type doesn't match)
    template <IsValueType T>
    constexpr T& get()
    {
#ifdef DEBUG_BUILD
        if (!holds_alternative<T>())
            throw std::bad_variant_access();
#endif
        return *reinterpret_cast<T*>(&m_data);
    }

    // Access the stored value as a const reference (throws if type doesn't match)
    template <IsValueType T>
    constexpr const T& get() const
    {
#ifdef DEBUG_BUILD
        if (!holds_alternative<T>())
            throw std::bad_variant_access();
#endif
        return *reinterpret_cast<const T*>(&m_data);
    }

    // Get the current type of the value
    Type get_type() const
    {
        return m_type;
    }

    static consteval ptrdiff_t data_offset()
    {
        return offsetof(Value, m_data);
    }

    static consteval ptrdiff_t type_offset()
    {
        return offsetof(Value, m_type);
    }

private:
    union Data
    {
        uint32_t uint32Value;
        uint64_t uint64Value;
        float floatValue;
        double doubleValue;
        uint128_t uint128Value;
        Reference referenceValue;

        Data()
            : uint32Value(0)
        {
        }
    } m_data;

    Type m_type;

    // Helper function to get the Type enum for a specific type
    template <typename T>
    static constexpr Type get_type_for()
    {
        if constexpr (std::is_same_v<T, uint32_t>)
            return Type::UInt32;
        if constexpr (std::is_same_v<T, uint64_t>)
            return Type::UInt64;
        if constexpr (std::is_same_v<T, float>)
            return Type::Float;
        if constexpr (std::is_same_v<T, double>)
            return Type::Double;
        if constexpr (std::is_same_v<T, uint128_t>)
            return Type::UInt128;
        if constexpr (std::is_same_v<T, Reference>)
            return Type::Reference;
        return Type::UInt32; // Should never reach here
    }
};

std::string value_to_string(Value value);
