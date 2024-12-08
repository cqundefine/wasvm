#include <Util.h>
#include <cassert>

void fill_buffer_with_random_data(uint8_t* data, size_t size)
{
#if LIBC_GLIBC_VERSION(2, 36)
    arc4random_buf(data, size);
#elif defined(OS_LINUX)
    FileStream randomStream("/dev/urandom");
    randomStream.read(data, size);
#else
    srand(time(nullptr));
    for (size_t i = 0; i < buffer_size; i++)
        data[i] = rand() % 256;
#endif
}

bool is_valid_utf8(const char* string)
{
    assert(string != nullptr);

    while (*string)
    {
        switch (std::countl_one(static_cast<unsigned char>(*string)))
        {
            [[unlikely]] case 4:
                ++string;
                if (std::countl_one(static_cast<unsigned char>(*string)) != 1)
                    return false;
                [[fallthrough]];
            [[unlikely]] case 3:
                ++string;
                if (std::countl_one(static_cast<unsigned char>(*string)) != 1)
                    return false;
                [[fallthrough]];
            [[unlikely]] case 2:
                ++string;
                if (std::countl_one(static_cast<unsigned char>(*string)) != 1)
                    return false;
                [[fallthrough]];
            [[likely]] case 0:
                ++string;
                break;
            [[unlikely]] default:
                return false;
        }
    }

    return true;
}
