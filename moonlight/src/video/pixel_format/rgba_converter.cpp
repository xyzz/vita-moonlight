#include <vita2d.h>

#include <cstring>

#include "pixel_format.hpp"

namespace PixelFormat
{

/**
 * Procesador RGBA basado en decodificación hardware
 * El decoder Vita produce RGBA directamente en hardware (rápido).
 * Sin downscale - rendimiento máximo del decoder.
 */
class RGBAHardwareProcessor : public IPixelProcessor
{
  private:
    int m_width;
    int m_height;
    uint32_t m_alignedWidth;
    uint32_t m_alignedHeight;

  public:
    RGBAHardwareProcessor()
        : m_width(0)
        , m_height(0)
        , m_alignedWidth(0)
        , m_alignedHeight(0)
    {
    }

    ~RGBAHardwareProcessor() override
    {
        cleanup();
    }

    int init(int width, int height, uint32_t alignedWidth, uint32_t alignedHeight) override
    {
        m_width         = width;
        m_height        = height;
        m_alignedWidth  = alignedWidth;
        m_alignedHeight = alignedHeight;
        return 0;
    }

    uint32_t getDecoderPixelFormat() const override
    {
        // The decoder must produce RGBA directly in hardware
        return SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    }

    uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) override
    {
        // Decode directly into the texture back buffer
        vita2d_texture* backTex = static_cast<vita2d_texture*>(backBuffer);
        if (backTex)
        {
            return static_cast<uint8_t*>(vita2d_texture_get_datap(backTex));
        }
        return nullptr;
    }

    int postProcess(uint8_t* decodedBuffer, void* outputTexture) override
    {
        // For direct RGBA there is no post-processing necessary
        // The decoder already wrote directly to the texture
        return 0;
    }

    void cleanup() override
    {
        // Nothing to clean, textures are handled externally
    }

    const char* getName() const override
    {
        return "RGBA Hardware Direct (No Downscale)";
    }

    bool requiresStagingBuffer() const override
    {
        // Does not require staging, writes directly to texture
        return false;
    }
};

// Factory implementation para RGBA
IPixelProcessor* createRGBAProcessor()
{
    return new RGBAHardwareProcessor();
}

} // namespace PixelFormat
