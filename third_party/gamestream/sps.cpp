#include "sps.h" // clase SpsContext + flags
#include <cstring>

using namespace gs;

SpsContext::SpsContext(int width, int height) : m_w(width), m_h(height) {
    m_stream = h264_new();
}

SpsContext::~SpsContext() {
    destroy();
}

SpsContext::SpsContext(SpsContext&& other) noexcept {
    m_stream = other.m_stream; other.m_stream = nullptr;
    m_w = other.m_w; m_h = other.m_h;
}

SpsContext& SpsContext::operator=(SpsContext&& other) noexcept {
    if (this != &other) {
        destroy();
        m_stream = other.m_stream; other.m_stream = nullptr;
        m_w = other.m_w; m_h = other.m_h;
    }
    return *this;
}

void SpsContext::destroy() {
    if (m_stream) {
        h264_free(m_stream);
        m_stream = nullptr;
    }
}

void SpsContext::fix(PLENTRY sps, int flags, uint8_t* out_buf, uint32_t* out_offset) {
    if (!m_stream || !sps || !out_buf || !out_offset) return;
    int start_len = sps->data[2] == 0x01 ? 3 : 4;
    if (read_nal_unit(m_stream, reinterpret_cast<uint8_t*>(sps->data + start_len), sps->length - start_len) < 0) {
        // fallback copiar
        std::memcpy(out_buf + *out_offset, sps->data, sps->length);
        *out_offset += sps->length;
        return;
    }
    if (m_w == 1280 && m_h == 720)
        m_stream->sps->level_idc = 32;
    else if (m_w == 1920 && m_h == 1080) {
        m_stream->sps->level_idc = 42;
        // Force height to 1088 (68 macroblocks) to satisfy Vita decoder alignment requirements
        m_stream->sps->pic_height_in_map_units_minus1 = 67;
        // Add cropping to display only 1080 lines
        m_stream->sps->frame_cropping_flag = 1;
        m_stream->sps->frame_crop_left_offset = 0;
        m_stream->sps->frame_crop_right_offset = 0;
        m_stream->sps->frame_crop_top_offset = 0;
        m_stream->sps->frame_crop_bottom_offset = 4; // 4 * 2 (SubHeightC) = 8 pixels cropped from bottom
    }
    m_stream->sps->num_ref_frames = 1;
    if (flags & GS_SPS_REMOVE_VST_FIXUP)
        m_stream->sps->vui.video_signal_type_present_flag = 0;
    if (flags & GS_SPS_REMOVE_CLI_FIXUP)
        m_stream->sps->vui.chroma_loc_info_present_flag = 0;
    if ((flags & GS_SPS_BITSTREAM_FIXUP) == GS_SPS_BITSTREAM_FIXUP) {
        if (!m_stream->sps->vui.bitstream_restriction_flag) {
            m_stream->sps->vui.bitstream_restriction_flag = 1;
            m_stream->sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
            m_stream->sps->vui.max_bits_per_mb_denom = 1;
            m_stream->sps->vui.log2_max_mv_length_horizontal = 16;
            m_stream->sps->vui.log2_max_mv_length_vertical = 16;
            m_stream->sps->vui.num_reorder_frames = 0;
        }
        m_stream->sps->vui.max_dec_frame_buffering = 1;
        m_stream->sps->vui.max_bytes_per_pic_denom = 2;
        m_stream->sps->vui.max_bits_per_mb_denom = 1;
    }
    std::memcpy(out_buf + *out_offset, sps->data, start_len);
    *out_offset += start_len;
    int wr = write_nal_unit(m_stream, out_buf + *out_offset, 128);
    if (wr < 0) {
        // fallback copiar original entera tras prefijo ya copiado
        std::memcpy(out_buf + *out_offset, sps->data + start_len, sps->length - start_len);
        *out_offset += (sps->length - start_len);
    } else {
        *out_offset += wr;
    }
}
