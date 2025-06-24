#pragma once

#include <string>
#include <string_view>

class Trap
{
public:
    Trap(std::string_view reason)
        : m_reason(reason)
    {
    }

    std::string_view reason() const { return m_reason; }

private:
    std::string m_reason;
};
