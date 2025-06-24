#pragma once

#include "Stream.h"
#include <cstring>

class MemoryStream : public Stream
{
public:
    MemoryStream(char* base, size_t size)
        : m_begin(base)
        , m_end(m_begin + size)
        , m_current(m_begin)
    {
    }

    virtual void read(void* buffer, size_t size) override
    {
        if (offset() + size > this->size())
            throw StreamReadException();
        memcpy(buffer, m_current, size);
        m_current += size;
    }

    virtual void move_to(size_t offset) override
    {
        if (offset > size())
            throw StreamReadException();
        m_current = m_begin + offset;
    }

    virtual size_t offset() const override { return m_current - m_begin; }
    virtual size_t size() const override { return m_end - m_begin; }

private:
    char* m_begin;
    char* m_end;
    char* m_current;
};
