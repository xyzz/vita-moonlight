#include "pixel_format.hpp"
#include "video/gxm_texture.hpp"

namespace PixelFormat
{

class RGBAHardwareProcessor : public IPixelProcessor
{
  public:
    int init(int, int, uint32_t, uint32_t) override
    {
        return 0;
    }

    uint32_t getDecoderPixelFormat() const override
    {
        return SCE_AVCDEC_PIXELFORMAT_RGBA8888;
    }

    uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) override
    {
        (void)frontBuffer;
        GxmTexture* backTex = static_cast<GxmTexture*>(backBuffer);
        if (backTex)
        {
            return static_cast<uint8_t*>(gxm_texture_get_datap(backTex));
        }
        return nullptr;
    }

    int postProcess(uint8_t*, void*) override
    {
        return 0;
    }

    void cleanup() override { }

    const char* getName() const override
    {
        return "RGBA Hardware Direct (No Downscale)";
    }

    bool requiresStagingBuffer() const override
    {
        return false;
    }
};

IPixelProcessor* createRGBAProcessor()
{
    return new RGBAHardwareProcessor();
}

} // namespace PixelFormat
