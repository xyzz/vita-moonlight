#include "controller/keyboard/keyboard_utf8.hpp"

#include <cstring>

#include "Limelight.h"

bool decode_single_utf8(const char* text, std::uint32_t& outCodepoint)
{
    if (!text || text[0] == '\0')
    {
        return false;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
    const std::size_t len      = std::strlen(text);

    if (bytes[0] <= 0x7F)
    {
        if (len != 1)
        {
            return false;
        }
        outCodepoint = bytes[0];
        return true;
    }

    if ((bytes[0] & 0xE0) == 0xC0)
    {
        if (len != 2 || (bytes[1] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        return true;
    }

    if ((bytes[0] & 0xF0) == 0xE0)
    {
        if (len != 3 || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        return true;
    }

    if ((bytes[0] & 0xF8) == 0xF0)
    {
        if (len != 4 || (bytes[1] & 0xC0) != 0x80 || (bytes[2] & 0xC0) != 0x80 || (bytes[3] & 0xC0) != 0x80)
        {
            return false;
        }
        outCodepoint = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        return true;
    }

    return false;
}

bool decode_single_utf8(const std::string& text, std::uint32_t& outCodepoint)
{
    if (text.empty())
    {
        return false;
    }
    return decode_single_utf8(text.c_str(), outCodepoint);
}

static bool encode_utf8_codepoint(std::uint32_t codepoint, char out[4], int& outLen)
{
    outLen = 0;
    if (codepoint <= 0x7F)
    {
        out[0] = static_cast<char>(codepoint);
        outLen = 1;
        return true;
    }
    if (codepoint <= 0x7FF)
    {
        out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 2;
        return true;
    }
    if (codepoint <= 0xFFFF)
    {
        out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 3;
        return true;
    }
    if (codepoint <= 0x10FFFF)
    {
        out[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        out[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        outLen = 4;
        return true;
    }
    return false;
}

bool send_utf8_codepoint(std::uint32_t codepoint)
{
    char buf[4] = { 0 };
    int len     = 0;
    if (!encode_utf8_codepoint(codepoint, buf, len))
    {
        return false;
    }
    return LiSendUtf8TextEvent(buf, static_cast<unsigned int>(len)) == 0;
}

std::uint32_t apply_shift_to_codepoint(std::uint32_t codepoint)
{
    if (codepoint >= 'a' && codepoint <= 'z')
    {
        return codepoint - 'a' + 'A';
    }
    if (codepoint == 0x00F1)
    {
        return 0x00D1;
    }
    return codepoint;
}

std::string utf8_from_codepoint(std::uint32_t codepoint)
{
    char buf[4] = { 0 };
    int len     = 0;
    if (!encode_utf8_codepoint(codepoint, buf, len))
    {
        return std::string();
    }
    return std::string(buf, static_cast<std::size_t>(len));
}
