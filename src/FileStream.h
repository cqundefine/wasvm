#pragma once

#include <Stream.h>
#include <cassert>
#include <cstdio>

class FileStream : public Stream
{
public:
    FileStream(const char* filename)
        : m_file(fopen(filename, "rb"))
    {
        assert(m_file != nullptr);

        fseek(m_file, 0, SEEK_END);
        m_size = ftell(m_file);
        fseek(m_file, 0, SEEK_SET);
    }

    ~FileStream()
    {
        fclose(m_file);
    }

    virtual void read(void* buffer, size_t size) override
    {
        size_t count = fread(buffer, size, 1, m_file);
        if (count == 0)
            throw StreamReadException();
    }

    virtual void move_to(size_t offset) override
    {
        fseek(m_file, offset, SEEK_SET);
    }

    virtual size_t offset() const override { return ftell(m_file); }
    virtual size_t size() const override { return m_size; }

    // Make noncopyable and nonmovable
    FileStream(const FileStream& other) = delete;
    FileStream& operator=(const FileStream& other) = delete;

    FileStream(FileStream&& other) = delete;
    FileStream& operator=(FileStream&& other) = delete;

private:
    FILE* m_file;
    size_t m_size;
};
