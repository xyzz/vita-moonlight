#pragma once
#include <Limelight.h>
#include <cstdint>
#include <h264_stream.h> // necesitamos la definición completa de h264_stream_t

// Flags heredados para el fix de SPS (mantener compatibilidad con llamadas existentes)
#define GS_SPS_BITSTREAM_FIXUP  0x01
#define GS_SPS_REMOVE_VST_FIXUP 0x02
#define GS_SPS_REMOVE_CLI_FIXUP 0x04

namespace gs {
class SpsContext {
public:
    SpsContext(int width, int height);
    ~SpsContext();
    SpsContext(const SpsContext&) = delete;
    SpsContext& operator=(const SpsContext&) = delete;
    SpsContext(SpsContext&&) noexcept;
    SpsContext& operator=(SpsContext&&) noexcept;

    void fix(PLENTRY sps, int flags, uint8_t* out_buf, uint32_t* out_offset);
private:
    h264_stream_t* m_stream = nullptr;
    int m_w = 0;
    int m_h = 0;
    void destroy();
};
}
