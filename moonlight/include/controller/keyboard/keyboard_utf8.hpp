#pragma once

#include <cstdint>
#include <string>

bool decode_single_utf8(const char* text, std::uint32_t& outCodepoint);
bool decode_single_utf8(const std::string& text, std::uint32_t& outCodepoint);
bool send_utf8_codepoint(std::uint32_t codepoint);
std::uint32_t apply_shift_to_codepoint(std::uint32_t codepoint);
std::string utf8_from_codepoint(std::uint32_t codepoint);
