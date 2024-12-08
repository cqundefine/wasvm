#pragma once

#include <Util.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <typeinfo>
#include <vector>

class Stream;

template <typename T>
concept IsStreamReadable = requires(Stream& stream) { { T::read_from_stream(stream) } -> std::same_as<T>; };

template <typename T>
concept IsUnsignedIntegral = std::is_integral_v<T> && std::is_unsigned_v<T>;

template <typename T>
concept IsSignedIntegral = std::is_integral_v<T> && std::is_signed_v<T>;

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

    void skip(ssize_t bytes)
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

    // FIXME: Make this take the actual size of the varuint
    template <IsUnsignedIntegral T>
    T read_leb()
    {
        T value = 0;
        T byte;
        uint8_t numberBytes = 0;
        do
        {
            byte = read_little_endian<uint8_t>();
            value |= ((byte & ~(1 << 7)) << (numberBytes * 7));
            numberBytes++;
        } while (byte & 0x80);

        return value;
    }

    template <IsSignedIntegral T, uint8_t bits>
    T read_leb()
    {
        std::make_unsigned_t<T> value = 0;
        std::make_unsigned_t<T> byte;
        uint8_t numberBytes = 0;
        do
        {
            byte = read_little_endian<uint8_t>();
            value |= ((byte & ~(1 << 7)) << (numberBytes * 7));
            numberBytes++;
        } while (byte & 0x80);

        if ((numberBytes * 7 < bits) && (byte & 0x40))
            value |= (~0ull << (numberBytes * 7));

        return (T)value;
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
        char* arr = (char*)malloc(size + 1);
        read((void*)arr, size);
        arr[size] = 0;
        if (!is_valid_utf8(arr))
            throw StreamReadException();
        std::string newString = std::string(arr, size);
        free(arr);
        return newString;
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
