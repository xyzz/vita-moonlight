#pragma once
#include <Limelight.h>
#include <cstddef>
#include <cstdint>
#include <h264_stream.h>

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

    [[nodiscard]] bool fix(PLENTRY sps, int flags, uint8_t* out_buf,
                           std::size_t out_capacity, std::size_t* out_offset);
private:
    h264_stream_t* m_stream = nullptr;
    int m_w = 0;
    int m_h = 0;
    void destroy();
};
}
