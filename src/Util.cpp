#include <Util.h>
#include <cassert>
#include <simdutf/simdutf.h>

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

bool is_valid_utf8(const std::string& string)
{
    return simdutf::validate_utf8_with_errors(string.c_str(), string.size()).error == simdutf::SUCCESS;
}
