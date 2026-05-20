#include <psp2/gxm.h>

#include <cstring>

#include "pixel_format.hpp"
#include "video/gxm_texture.hpp"

namespace PixelFormat
{

/**
 * Procesador YUV para ruta GPU.
 * El decoder escribe NV12 directamente en una textura CSC de GXM.
 * La conversión YUV->RGB la hace la GPU durante el muestreo de la textura.
 */
class YUVGpuRenderer : public IPixelProcessor
{
  private:
    int m_width;
    int m_height;
    uint32_t m_alignedWidth;
    uint32_t m_alignedHeight;
    uint8_t* m_lastDecodeTarget;

  public:
    YUVGpuRenderer()
        : m_width(0)
        , m_height(0)
        , m_alignedWidth(0)
        , m_alignedHeight(0)
        , m_lastDecodeTarget(nullptr)
    {
    }

    ~YUVGpuRenderer() override
    {
        cleanup();
    }

    int init(int width, int height, uint32_t alignedWidth, uint32_t alignedHeight) override
    {
        m_width            = width;
        m_height           = height;
        m_alignedWidth     = alignedWidth;
        m_alignedHeight    = alignedHeight;
        m_lastDecodeTarget = nullptr;
        return 0;
    }

    uint32_t getDecoderPixelFormat() const override
    {
        // Decoder output is NV12/YUV420 raster, compatible with CSC texture path
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
        // GPU CSC path: decoder already wrote in-place into the target texture.
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

// Factory implementation para YUV
IPixelProcessor* createYUVProcessor()
{
    return new YUVGpuRenderer();
}

} // namespace PixelFormat
