#include "pixel_format.hpp"

namespace PixelFormat
{

// Forward declarations
IPixelProcessor* createRGBAProcessor();
IPixelProcessor* createYUVProcessor();

IPixelProcessor* createProcessor(int mode)
{
    switch (mode)
    {
        case MODE_RGBA_HARDWARE:
            return createRGBAProcessor();

        case MODE_YUV_GPU:
            return createYUVProcessor();

        default:
            // By default use RGBA (compatible)
            return createRGBAProcessor();
    }
}

void destroyProcessor(IPixelProcessor* processor)
{
    if (processor)
    {
        processor->cleanup();
        delete processor;
    }
}

} // namespace PixelFormat
