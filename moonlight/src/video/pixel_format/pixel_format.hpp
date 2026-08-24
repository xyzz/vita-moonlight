#pragma once

#include <psp2/videodec.h>

#include <cstddef>
#include <cstdint>

namespace PixelFormat
{

class IPixelProcessor
{
  public:
    virtual ~IPixelProcessor() = default;

    virtual int init(int width, int height, uint32_t alignedWidth, uint32_t alignedHeight) = 0;

    virtual uint32_t getDecoderPixelFormat() const = 0;

    virtual uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) = 0;

    virtual int postProcess(uint8_t* decodedBuffer, void* outputTexture) = 0;

    virtual void cleanup() = 0;

    virtual const char* getName() const = 0;

    virtual bool requiresStagingBuffer() const = 0;
};

IPixelProcessor* createProcessor(int mode);
void destroyProcessor(IPixelProcessor* processor);

enum ProcessorMode
{
    MODE_RGBA_HARDWARE = 0,
    MODE_YUV_GPU       = 1
};

// Decoder pixel format constants
#ifndef SCE_AVCDEC_PIXELFORMAT_RGBA8888
#define SCE_AVCDEC_PIXELFORMAT_RGBA8888 0x0
#endif

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER
#define SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER 0x10
#endif

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER
#define SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER 0x20
#endif

} // namespace PixelFormat
