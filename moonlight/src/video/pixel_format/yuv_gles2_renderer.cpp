#include <psp2/gxm.h>
#include <vita2d.h>

#include <cstring>

#include "pixel_format.hpp"

extern "C"
{
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace PixelFormat
{

/**
 * Procesador YUV optimizado con FFmpeg swscale
 * Usa swscale con NEON para conversión YUV420→RGBA acelerada por CPU.
 * Más rápido que conversión naive pero más lento que GPU shaders.
 */
class YUVSwscaleRenderer : public IPixelProcessor
{
  private:
    int m_width;
    int m_height;
    uint32_t m_alignedWidth;
    uint32_t m_alignedHeight;

    // Buffer for YUV of the decoder
    uint8_t* m_yuvBuffer;
    size_t m_yuvBufferSize;

    // FFmpeg swscale context
    SwsContext* m_swsContext;

    // Buffers para frames FFmpeg
    uint8_t* m_srcData[4]; // YUV planes
    int m_srcLinesize[4]; // YUV strides

    uint8_t* m_dstData[4]; // RGBA output
    int m_dstLinesize[4]; // RGBA stride

  public:
    YUVSwscaleRenderer()
        : m_width(0)
        , m_height(0)
        , m_alignedWidth(0)
        , m_alignedHeight(0)
        , m_yuvBuffer(nullptr)
        , m_yuvBufferSize(0)
        , m_swsContext(nullptr)
    {
        memset(m_srcData, 0, sizeof(m_srcData));
        memset(m_srcLinesize, 0, sizeof(m_srcLinesize));
        memset(m_dstData, 0, sizeof(m_dstData));
        memset(m_dstLinesize, 0, sizeof(m_dstLinesize));
    }

    ~YUVSwscaleRenderer() override
    {
        cleanup();
    }

    int init(int width, int height, uint32_t alignedWidth, uint32_t alignedHeight) override
    {
        m_width         = width;
        m_height        = height;
        m_alignedWidth  = alignedWidth;
        m_alignedHeight = alignedHeight;

        // Allocate YUV buffer for decoder output
        size_t ySize    = (size_t)alignedWidth * alignedHeight;
        size_t uvSize   = ySize / 4;
        m_yuvBufferSize = ySize + 2 * uvSize;

        m_yuvBuffer = new uint8_t[m_yuvBufferSize + 4096]; // extra for alignment
        if (!m_yuvBuffer)
        {
            return -1;
        }

        // Setup source NV12 planes (Y planar + UV interleaved)
        m_srcData[0] = m_yuvBuffer; // Y plane
        m_srcData[1] = m_srcData[0] + ySize; // UV interleaved
        m_srcData[2] = nullptr; // No separate V
        m_srcData[3] = nullptr;

        m_srcLinesize[0] = alignedWidth; // Y stride
        m_srcLinesize[1] = alignedWidth; // UV stride (interleaved, full width)
        m_srcLinesize[2] = 0;
        m_srcLinesize[3] = 0;

        // Destination RGBA stride (will point to vita2d texture)
        m_dstLinesize[0] = alignedWidth * 4; // RGBA = 4 bytes/pixel
        m_dstLinesize[1] = 0;
        m_dstLinesize[2] = 0;
        m_dstLinesize[3] = 0;

        // Create swscale context with NEON optimizations
        // Source: NV12 (what Vita decoder produces)
        m_swsContext = sws_getContext(
            alignedWidth, alignedHeight, AV_PIX_FMT_NV12, // src (NV12 = YUV420 RASTER)
            alignedWidth, alignedHeight, AV_PIX_FMT_RGBA, // dst
            SWS_FAST_BILINEAR, // flags (fast)
            nullptr, nullptr, nullptr);

        if (!m_swsContext)
        {
            delete[] m_yuvBuffer;
            m_yuvBuffer = nullptr;
            return -1;
        }

        return 0;
    }

    uint32_t getDecoderPixelFormat() const override
    {
        // The decoder must produce YUV420
        return SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER;
    }

    uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) override
    {
        // Decode to internal YUV buffer
        return m_yuvBuffer;
    }

    int postProcess(uint8_t* decodedBuffer, void* outputTexture) override
    {
        if (!m_swsContext || !decodedBuffer || !outputTexture)
        {
            return -1;
        }

        // Get vita2d texture data pointer
        vita2d_texture* tex = static_cast<vita2d_texture*>(outputTexture);
        m_dstData[0]        = static_cast<uint8_t*>(vita2d_texture_get_datap(tex));

        if (!m_dstData[0])
        {
            return -1;
        }

        // Perform YUV→RGBA conversion with swscale (uses NEON automatically)
        int ret = sws_scale(
            m_swsContext,
            m_srcData, m_srcLinesize,
            0, m_alignedHeight,
            m_dstData, m_dstLinesize);

        return (ret > 0) ? 0 : -1;
    }

    void cleanup() override
    {
        if (m_swsContext)
        {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }

        if (m_yuvBuffer)
        {
            delete[] m_yuvBuffer;
            m_yuvBuffer = nullptr;
        }
        m_yuvBufferSize = 0;
    }

    const char* getName() const override
    {
        return "YUV FFmpeg swscale (NEON)";
    }

    bool requiresStagingBuffer() const override
    {
        return false; // YUV buffer interno
    }
};

// Factory implementation para YUV
IPixelProcessor* createYUVProcessor()
{
    return new YUVSwscaleRenderer();
}

} // namespace PixelFormat
