#pragma once

#include <cstdint>

namespace vita
{

std::uint64_t monotonicMicroseconds();
std::uint64_t monotonicMilliseconds();
void delayBeforeGxmTeardown();

}
