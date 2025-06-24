#pragma once

#include <SIMD.h>
#include <Trap.h>
#include <Type.h>
#include <Util.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

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

class RealModule;

struct Reference
{
    ReferenceType type;
    std::optional<uint32_t> index;
    RealModule* module;
};

#ifdef DEBUG_BUILD
template <typename T>
inline constexpr const char* value_type_name = []() {
    static_assert(false);
};

template <>
inline constexpr const char* value_type_name<uint32_t> = "i32";

template <>
inline constexpr const char* value_type_name<uint64_t> = "i64";

template <>
inline constexpr const char* value_type_name<float> = "f32";

template <>
inline constexpr const char* value_type_name<double> = "f64";

template <>
inline constexpr const char* value_type_name<uint128_t> = "v128";

template <>
inline constexpr const char* value_type_name<Reference> = "funcref / externref";
#endif

template <typename T, typename = void>
struct ToValueTypeHelper
{
    using type = T;
};

template <typename T>
struct ToValueTypeHelper<T, std::enable_if_t<std::is_integral_v<T>>>
{
    using type = std::make_unsigned_t<T>;
};

static_assert(!std::is_integral_v<int64x2_t>);

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

template <typename T>
concept IsValueType = IsAnyOf<ToValueType<T>, uint32_t, uint64_t, float, double, uint128_t, Reference>;

class Value
{
    friend class ValueStack;

public:
    constexpr Value()
        : m_type(Type::UInt32)
    {
        new (&m_data) uint32_t(0);
    }

    template <IsValueType T>
    constexpr Value(const T& value)
        : m_type(get_type_for<ToValueType<T>>())
    {
        new (&m_data) ToValueType<T>((ToValueType<T>)value);
    }

    constexpr Value(const Value&) = default;
    constexpr Value(Value&&) noexcept = default;
    constexpr Value& operator=(const Value&) = default;
    constexpr Value& operator=(Value&&) noexcept = default;

    template <IsValueType T>
    constexpr bool holds_alternative() const
    {
        return m_type == get_type_for<T>();
    }

    template <IsValueType T>
    constexpr T& get()
    {
#ifdef DEBUG_BUILD
        if (!holds_alternative<T>())
            throw Trap("Invalid get on value");
#endif
        return *std::bit_cast<T*>(&m_data);
    }

    template <IsValueType T>
    constexpr const T& get() const
    {
#ifdef DEBUG_BUILD
        if (!holds_alternative<T>())
            throw Trap("Invalid get on value");
#endif
        return *std::bit_cast<const T*>(&m_data);
    }

    bool operator==(const Value& other) const;

    constexpr Type get_type() const
    {
        if (holds_alternative<uint32_t>())
            return ::Type::i32;
        if (holds_alternative<uint64_t>())
            return ::Type::i64;
        if (holds_alternative<float>())
            return ::Type::f32;
        if (holds_alternative<double>())
            return ::Type::f64;
        if (holds_alternative<uint128_t>())
            return ::Type::v128;
        if (holds_alternative<Reference>())
        {
            if (get<Reference>().type == ReferenceType::Function)
                return ::Type::funcref;
            if (get<Reference>().type == ReferenceType::Extern)
                return ::Type::externref;
        }

        UNREACHABLE();
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

    enum class Type
    {
        UInt32,
        UInt64,
        Float,
        Double,
        UInt128,
        Reference
    } m_type;

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

        UNREACHABLE();
    }
};

template <>
struct std::formatter<Value>
{
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return std::cbegin(ctx);
    }

    auto format(const Value& obj, std::format_context& ctx) const
    {
        auto type = get_type_name(obj.get_type());
        if (obj.holds_alternative<uint32_t>())
            return std::format_to(ctx.out(), "{}({})", type, obj.get<uint32_t>());
        if (obj.holds_alternative<uint64_t>())
            return std::format_to(ctx.out(), "{}({})", type, obj.get<uint64_t>());
        if (obj.holds_alternative<float>())
            return std::format_to(ctx.out(), "{}({})", type, obj.get<float>());
        if (obj.holds_alternative<double>())
            return std::format_to(ctx.out(), "{}({})", type, obj.get<double>());
        if (obj.holds_alternative<uint128_t>())
            return std::format_to(ctx.out(), "{}({})", type, obj.get<uint128_t>());
        if (obj.holds_alternative<Reference>())
        {
            const auto reference = obj.get<Reference>();
            if (reference.index)
                return std::format_to(ctx.out(), "{}(null)", type);
            else
                return std::format_to(ctx.out(), "{}({})", type, *reference.index);
        }

        UNREACHABLE();
    }
};
