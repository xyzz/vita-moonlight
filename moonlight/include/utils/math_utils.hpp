#pragma once

namespace moonlight
{

constexpr float clampNormalized(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

}
