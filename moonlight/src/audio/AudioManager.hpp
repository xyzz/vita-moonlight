#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// AudioManager: interfaz para manejar paquetes de audio entrantes (stream Moonlight)
// Objetivos próximos:
//  - Buffer JIT de audio para sincronización con video
//  - Estadísticas de jitter, pérdidas y underruns
//  - Posible resample si host cambia frecuencia
//  - Mixer simple para futuros sonidos UI

struct AudioPacket
{
    uint32_t frameIndex; // índice o número de bloque
    uint64_t ptsMs; // timestamp estimado (ms)
    std::vector<uint8_t> data; // datos codificados recibidos (por ahora passthrough)
};

struct AudioStats
{
    uint32_t packetsReceived     = 0;
    uint32_t packetsLost         = 0;
    uint32_t unrecoverableBlocks = 0;
    uint32_t latePackets         = 0;
    uint32_t queuedFrames        = 0;
    uint32_t underruns           = 0;
    uint64_t lastPtsMs           = 0;
};

class AudioManager
{
  public:
    static AudioManager& instance();

    // Alimentar un paquete de audio recibido (desempaquetado desde red)
    void pushPacket(const AudioPacket& pkt);

    // Llamado periódicamente (ej. desde loop principal) para drenar y reproducir
    void update(uint64_t nowMs);

    // Obtener stats (snapshot)
    AudioStats getStats() const;

    // Reset (nueva sesión)
    void reset();

    // Configuración inicial (formato, sample rate, etc.)
    void configure(int sampleRate, int channels, const std::string& codecName);

  private:
    AudioManager()                               = default;
    AudioManager(const AudioManager&)            = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    mutable AudioStats stats;
    int sampleRate = 0;
    int channels   = 0;
    std::string codec;
    // TODO: implementar cola circular eficiente (por ahora vector simple)
    std::vector<AudioPacket> queue;
};
