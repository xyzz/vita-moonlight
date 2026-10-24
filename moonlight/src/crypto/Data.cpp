#include "Data.hpp"

#include <string.h>

#include <borealis.hpp>
#include <cstdlib>
#ifdef __vita__
#include <psp2/kernel/rng.h>
#else
#include <random>
#endif

#include "debug.hpp"

Data::Data(unsigned char* bytes, size_t size)
{
    if (bytes && size > 0)
    {
        m_bytes       = (unsigned char*)malloc(size + 1);
        m_bytes[size] = '\0';
        memcpy(m_bytes, bytes, size);
        m_size = size;
    }
}

Data::Data(size_t capacity)
{
    m_bytes = (unsigned char*)malloc(capacity + 1);
    memset(m_bytes, 0, capacity + 1);
    m_bytes[capacity] = '\0';
    m_size            = capacity;
}

Data::~Data()
{
    if (m_bytes)
    {
        free(m_bytes);
    }
}

Data Data::subdata(size_t start, size_t size)
{
    if (start + size > m_size)
    {
        vita_log::error("Data: Invalid data length...");
        exit(-1);
    }
    return Data(&m_bytes[start], size);
}

Data Data::append(Data other)
{
    if (is_empty())
    {
        return other;
    }

    Data data(m_size + other.m_size);
    memcpy(data.m_bytes, m_bytes, m_size);
    memcpy(&data.m_bytes[m_size], other.m_bytes, other.m_size);
    return data;
}

Data::Data(const Data& that)
    : Data(0)
{
    if (m_bytes)
    {
        free(m_bytes);
    }

    m_bytes = (unsigned char*)malloc(that.size() + 1);
    memcpy(m_bytes, that.m_bytes, that.m_size);
    m_bytes[that.m_size] = '\0';
    m_size               = that.m_size;
}

Data& Data::operator=(const Data& that)
{
    if (this != &that)
    {
        if (m_bytes)
        {
            free(m_bytes);
        }

        m_bytes = (unsigned char*)malloc(that.m_size + 1);
        memcpy(m_bytes, that.m_bytes, that.m_size);
        m_bytes[that.m_size] = '\0';
        m_size               = that.m_size;
    }
    return *this;
}

Data Data::random_bytes(size_t size)
{
    if (size == 0)
        return Data();

    Data result(size);
#ifdef __vita__
    for (size_t offset = 0; offset < size;)
    {
        size_t count = size - offset;
        if (count > 64)
            count = 64;
        if (sceKernelGetRandomNumber(result.m_bytes + offset, count) < 0)
            return Data();
        offset += count;
    }
#else
    std::random_device random;
    for (size_t i = 0; i < size; ++i)
        result.m_bytes[i] = static_cast<unsigned char>(random());
#endif
    return result;
}

Data Data::read_from_file(std::string path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return Data();

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return Data();
    }

    long length = ftell(f);
    if (length <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return Data();
    }

    char* buffer = (char*)malloc((size_t)length);
    if (!buffer)
    {
        fclose(f);
        return Data();
    }

    size_t bytesRead = fread(buffer, 1, (size_t)length, f);
    fclose(f);
    if (bytesRead != (size_t)length)
    {
        free(buffer);
        return Data();
    }

    Data data(buffer, bytesRead);
    free(buffer);
    return data;
}

void Data::write_to_file(std::string path)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (f)
    {
        fwrite(m_bytes, m_size, 1, f);
        fclose(f);
    }
    else
    {
        vita_log::error("Data: Path not found: %s", path.c_str());
    }
}

Data Data::hex_to_bytes() const
{
    if (m_size == 0 || (m_size & 1) != 0)
        return Data();

    auto hex_digit = [](unsigned char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    Data result(m_size / 2);
    for (size_t i = 0; i < m_size; i += 2)
    {
        int high = hex_digit(m_bytes[i]);
        int low  = hex_digit(m_bytes[i + 1]);
        if (high < 0 || low < 0)
            return Data();
        result.m_bytes[i / 2] = static_cast<unsigned char>((high << 4) | low);
    }
    return result;
}

Data Data::hex() const
{
    if (!m_size)
    {
        char end = '\n';
        return Data(&end, 1);
    }

    int counter = 0;
    Data hex(m_size * 2);
    char fmt[3] = { '\0', '\0', '\0' };
    for (int i = 0; i < m_size; i++)
    {
        sprintf(fmt, "%02X", m_bytes[i]);
        hex.m_bytes[counter++] = fmt[0];
        hex.m_bytes[counter++] = fmt[1];
    }
    hex.m_bytes[m_size * 2] = '\0';
    return hex;
}
