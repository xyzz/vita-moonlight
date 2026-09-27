#pragma once

namespace moonlight
{

enum class FrontendMode
{
    Legacy,
    Borealis,
};

bool saveFrontendMode(FrontendMode mode);
bool restartWithFrontend(FrontendMode mode);

}
