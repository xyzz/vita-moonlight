#pragma once

#include <psp2/videodec.h>

#include <cstddef>
#include <cstdint>

namespace PixelFormat
{

/**
 * Interfaz para procesadores de formato de píxel.
 * El decoder de Vita produce directamente RGBA o YUV en hardware.
 * Los procesadores solo manejan el output del decoder.
 */
class IPixelProcessor
{
  public:
    virtual ~IPixelProcessor() = default;

    /**
     * Inicializa el procesador con las dimensiones del stream
     */
    virtual int init(int width, int height, uint32_t alignedWidth, uint32_t alignedHeight) = 0;

    /**
     * Devuelve el formato de píxel que el decoder debe producir
     */
    virtual uint32_t getDecoderPixelFormat() const = 0;

    /**
     * Devuelve el buffer donde el decoder debe escribir
     * @param frontBuffer Buffer front (para doble buffer)
     * @param backBuffer Buffer back (para doble buffer)
     * @return Puntero al buffer de destino para el decoder
     */
    virtual uint8_t* getDecodeTarget(void* frontBuffer, void* backBuffer) = 0;

    /**
     * Post-procesa el frame después de la decodificación
     * @param decodedBuffer Buffer donde el decoder escribió
     * @param outputTexture Textura de salida (GxmTexture*)
     * @return 0 si exitoso
     */
    virtual int postProcess(uint8_t* decodedBuffer, void* outputTexture) = 0;

    /**
     * Limpia recursos
     */
    virtual void cleanup() = 0;

    /**
     * Devuelve el nombre del procesador
     */
    virtual const char* getName() const = 0;

    /**
     * Devuelve true si requiere staging buffer
     */
    virtual bool requiresStagingBuffer() const = 0;
};

/**
 * Factory para crear procesadores
 */
IPixelProcessor* createProcessor(int mode);

/**
 * Libera un procesador
 */
void destroyProcessor(IPixelProcessor* processor);

// Available modes
enum ProcessorMode
{
    MODE_RGBA_HARDWARE = 0, // Decoder produces RGBA in hardware (fast, compatible)
    MODE_YUV_GPU       = 1 // Decoder produce YUV para GPU shaders (futuro, 60 FPS)
};

// Decoder pixel format constants
#ifndef SCE_AVCDEC_PIXELFORMAT_RGBA8888
#define SCE_AVCDEC_PIXELFORMAT_RGBA8888 0x0
#endif

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER
#define SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER 0x1
#endif

#ifndef SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER
#define SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER 0x2
#endif

} // namespace PixelFormat
