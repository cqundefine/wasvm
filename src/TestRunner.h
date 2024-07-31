#pragma once

#include <cstdint>

struct TestStats
{
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    uint32_t failed_to_load;
};

TestStats run_tests(const char* path);
