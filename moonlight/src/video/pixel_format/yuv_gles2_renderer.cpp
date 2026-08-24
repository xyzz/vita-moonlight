#include <psp2/gxm.h>

#include "pixel_format.hpp"
#include "video/gxm_texture.hpp"

namespace PixelFormat
{

class YUVGpuRenderer : public IPixelProcessor
{
  private:
    uint8_t* m_lastDecodeTarget = nullptr;

  public:
    int init(int, int, uint32_t, uint32_t) override
    {
        m_lastDecodeTarget = nullptr;
        return 0;
    }

    uint32_t getDecoderPixelFormat() const override
    {
        return SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER;
    }

    uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) override
    {
        (void)frontBuffer;
        GxmTexture* backTex = static_cast<GxmTexture*>(backBuffer);
        if (!backTex)
        {
            m_lastDecodeTarget = nullptr;
            return nullptr;
        }
        m_lastDecodeTarget = static_cast<uint8_t*>(gxm_texture_get_datap(backTex));
        return m_lastDecodeTarget;
    }

    int postProcess(uint8_t* decodedBuffer, void* outputTexture) override
    {
        (void)outputTexture;
        if (!decodedBuffer)
        {
            return -1;
        }
        return (decodedBuffer == m_lastDecodeTarget) ? 0 : -1;
    }

    void cleanup() override
    {
        m_lastDecodeTarget = nullptr;
    }

    const char* getName() const override
    {
        return "YUV GPU CSC (NV12 direct)";
    }

    bool requiresStagingBuffer() const override
    {
        return false;
    }
};

IPixelProcessor* createYUVProcessor()
{
    return new YUVGpuRenderer();
}

} // namespace PixelFormat
