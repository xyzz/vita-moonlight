#include "sps.h"
#include <cstring>
#include <limits>

namespace gs {

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

bool SpsContext::fix(PLENTRY sps, int flags, uint8_t* out_buf,
                     std::size_t out_capacity, std::size_t* out_offset) {
    if (!sps || !sps->data || sps->length <= 0 || !out_buf || !out_offset ||
        *out_offset > out_capacity) {
        return false;
    }

    const std::size_t original_offset = *out_offset;
    const std::size_t sps_length = static_cast<std::size_t>(sps->length);
    const auto append_original = [&]() {
        if (sps_length > out_capacity - original_offset) {
            return false;
        }
        std::memcpy(out_buf + original_offset, sps->data, sps_length);
        *out_offset = original_offset + sps_length;
        return true;
    };

    if (!m_stream) {
        return append_original();
    }

    std::size_t start_len = 0;
    if (sps_length >= 4 && sps->data[0] == 0 && sps->data[1] == 0) {
        if (sps->data[2] == 1) {
            start_len = 3;
        } else if (sps_length >= 5 && sps->data[2] == 0 && sps->data[3] == 1) {
            start_len = 4;
        }
    }

    const std::size_t nal_length = sps_length - start_len;
    if (start_len == 0 || nal_length == 0 ||
        nal_length > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        read_nal_unit(m_stream, reinterpret_cast<uint8_t*>(sps->data + start_len),
                      static_cast<int>(nal_length)) < 0 ||
        !m_stream->nal || m_stream->nal->nal_unit_type != NAL_UNIT_TYPE_SPS) {
        return append_original();
    }

    if (m_w == 1280 && m_h == 720)
        m_stream->sps->level_idc = 32;
    else if (m_w == 1920 && m_h == 1080) {
        m_stream->sps->level_idc = 42;
        // The Vita decoder requires a macroblock-aligned 1088-line coded height.
        m_stream->sps->pic_height_in_map_units_minus1 = 67;
        m_stream->sps->frame_cropping_flag = 1;
        m_stream->sps->frame_crop_left_offset = 0;
        m_stream->sps->frame_crop_right_offset = 0;
        m_stream->sps->frame_crop_top_offset = 0;
        m_stream->sps->frame_crop_bottom_offset = 4;
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

    if (start_len > out_capacity - original_offset) {
        return false;
    }
    const std::size_t nal_capacity = out_capacity - original_offset - start_len;
    const int writer_capacity = static_cast<int>(
        nal_capacity > static_cast<std::size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : nal_capacity);
    if (writer_capacity == 0) {
        return append_original();
    }

    std::memcpy(out_buf + original_offset, sps->data, start_len);
    const int written = write_nal_unit(m_stream, out_buf + original_offset + start_len,
                                       writer_capacity);
    if (written <= 0 || written > writer_capacity) {
        return append_original();
    }

    *out_offset = original_offset + start_len + static_cast<std::size_t>(written);
    return true;
}

} // namespace gs
