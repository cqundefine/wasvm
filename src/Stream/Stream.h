#pragma once

#include "Util/Util.h"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Stream;

template <typename T>
concept IsStreamReadable = requires(Stream& stream) { { T::read_from_stream(stream) } -> std::same_as<T>; };

struct StreamReadException : public std::exception
{
    const char* what() const throw()
    {
        return "StreamReadException";
    }
};

class Stream
{
public:
    Stream() = default;

    virtual void read(void* buffer, size_t size) = 0;
    virtual void move_to(size_t offset) = 0;
    virtual void reset() { move_to(0); }

    virtual size_t offset() const = 0;
    virtual size_t size() const = 0;

    void skip(int64_t bytes)
    {
        move_to(offset() + bytes);
    }

    bool eof() const
    {
        return offset() == size();
    }

    template <typename T>
    T read_little_endian()
    {
        T value;
        read((void*)&value, sizeof(T));
        // return HostToLittleEndian(value);
        return value;
    }

    std::vector<uint8_t> read_leb_as_bytes()
    {
        std::vector<uint8_t> bytes;
        uint8_t byte;
        do
        {
            byte = read_little_endian<uint8_t>();
            bytes.push_back(byte);
        } while (byte & 0x80);

        return bytes;
    }

    template <std::unsigned_integral T>
    T read_leb()
    {
        T result {};
        size_t num_bytes = 0;

        while (true)
        {
            if (eof())
                throw StreamReadException(/*"Stream reached end-of-file while reading LEB128 value"*/);

            auto byte = read_little_endian<uint8_t>();

            T masked_byte = byte & ~(1 << 7);
            const bool shift_too_large_for_result = num_bytes * 7 > sizeof(T) * 8;
            if (shift_too_large_for_result)
                throw StreamReadException(/*"Read value contains more bits than fit the chosen ValueType"*/);

            const bool shift_too_large_for_byte = ((masked_byte << (num_bytes * 7)) >> (num_bytes * 7)) != masked_byte;
            if (shift_too_large_for_byte)
                throw StreamReadException(/*"Read byte is too large to fit the chosen ValueType"*/);

            result = (result) | (masked_byte << (num_bytes * 7));
            if (!(byte & (1 << 7)))
                break;
            ++num_bytes;
        }

        return result;
    }

    template <std::signed_integral T>
    T read_leb()
    {
        constexpr auto BITS = sizeof(T) * 8;

        T result = 0;
        uint32_t shift = 0;
        uint8_t byte = 0;

        do
        {
            if (eof())
                throw StreamReadException(/*"Stream reached end-of-file while reading LEB128 value"*/);
            byte = read_little_endian<uint8_t>();
            result |= (T)(byte & 0x7F) << shift;

            if (shift >= BITS - 7)
            {
                const bool has_continuation = (byte & 0x80);
                T sign_and_unused = (int8_t)(byte << 1) >> (BITS - shift);
                if (has_continuation)
                    throw StreamReadException(/*"Read value contains more bits than fit the chosen ValueType"*/);
                if (sign_and_unused != 0 && sign_and_unused != -1)
                    throw StreamReadException(/*"Read byte is too large to fit the chosen ValueType"*/);
                return result;
            }

            shift += 7;
        } while (byte & 0x80);

        if (shift < BITS && (byte & 0x40))
            result |= ((T)~0 << shift);

        return result;
    }

    template <typename T>
    T read_typed()
    {
        static_assert(false, "Reading this type from stream is not implemented");
    }

    template <IsStreamReadable T>
    T read_typed()
    {
        return T::read_from_stream(*this);
    }

    template <>
    uint8_t read_typed()
    {
        return read_little_endian<uint8_t>();
    }

    template <>
    uint32_t read_typed()
    {
        return read_leb<uint32_t>();
    }

    template <>
    std::string read_typed()
    {
        uint32_t size = read_leb<uint32_t>();

        std::string string(size, '\0');
        read(string.data(), size);

        if (!is_valid_utf8(string))
            throw StreamReadException();

        return string;
    }

    template <typename T>
    std::vector<T> read_vec()
    {
        uint32_t size = read_leb<uint32_t>();
        std::vector<T> vec;
        for (uint32_t i = 0; i < size; i++)
        {
            vec.push_back(read_typed<T>());
        }
        return vec;
    }

    template <typename T, T(function)(Stream&)>
    std::vector<T> read_vec_with_function()
    {
        uint32_t size = read_leb<uint32_t>();
        std::vector<T> vec;
        for (uint32_t i = 0; i < size; i++)
        {
            vec.push_back(function(*this));
        }
        return vec;
    }
};
