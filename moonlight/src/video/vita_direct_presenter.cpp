#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>

#include <cstdint>

#include "debug.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"

extern "C" const SceGxmProgram texture_v_gxp_start;
extern "C" const SceGxmProgram texture_f_gxp_start;

struct Vita2dTextureVertex
{
    float x, y, z;
    float u, v;
};

static bool s_dp_active = false;

static SceGxmVertexProgram* s_vert_prog          = nullptr;
static SceGxmFragmentProgram* s_frag_prog        = nullptr;
static SceGxmShaderPatcherId s_vert_id           = nullptr;
static SceGxmShaderPatcherId s_frag_id           = nullptr;
static const SceGxmProgramParameter* s_wvp_param = nullptr;

static SceUID s_vertices_uid           = -1;
static Vita2dTextureVertex* s_vertices = nullptr;

static SceUID s_indices_uid = -1;
static uint16_t* s_indices  = nullptr;

static void* gpu_alloc(SceKernelMemBlockType type, unsigned int size, unsigned int align,
    SceGxmMemoryAttribFlags attribs, SceUID* uid)
{
    void* mem;
    if (align < 4096)
        align = 4096;
    size = (size + align - 1) & ~(align - 1);
    *uid = sceKernelAllocMemBlock("dp_gpu_mem", type, size, nullptr);
    if (*uid < 0)
        return nullptr;
    if (sceKernelGetMemBlockBase(*uid, &mem) < 0)
    {
        sceKernelFreeMemBlock(*uid);
        *uid = -1;
        return nullptr;
    }
    if (sceGxmMapMemory(mem, size, attribs) < 0)
    {
        sceKernelFreeMemBlock(*uid);
        *uid = -1;
        return nullptr;
    }
    return mem;
}

static void gpu_free(SceUID uid)
{
    if (uid < 0)
        return;
    void* mem = NULL;
    if (sceKernelGetMemBlockBase(uid, &mem) >= 0 && mem)
    {
        sceGxmUnmapMemory(mem);
    }
    sceKernelFreeMemBlock(uid);
}

static void matrix_ortho(float* m, float left, float right, float bottom, float top, float near, float far)
{
    m[0] = 2.0f / (right - left);
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;

    m[4] = 0.0f;
    m[5] = 2.0f / (top - bottom);
    m[6] = 0.0f;
    m[7] = 0.0f;

    m[8]  = 0.0f;
    m[9]  = 0.0f;
    m[10] = -2.0f / (far - near);
    m[11] = 0.0f;

    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far + near) / (far - near);
    m[15] = 1.0f;
}

extern "C"
{

    void vita_dp_fini(void); // used by the init failure paths below

    int vita_dp_init(void)
    {
        if (s_dp_active)
            return 0;

        NVGXMwindow* win = gxmGetWindow();
        if (!win || !win->context || !win->fb || !win->shader_patcher)
        {
            vita_log::error("[DirectPresenter] No GXM window/patcher available");
            return -1;
        }

        sceGxmSetYuvProfile(win->context, 0, SCE_GXM_YUV_PROFILE_BT709_STANDARD);

        int regVert = sceGxmShaderPatcherRegisterProgram(win->shader_patcher, &texture_v_gxp_start, &s_vert_id);
        int regFrag = sceGxmShaderPatcherRegisterProgram(win->shader_patcher, &texture_f_gxp_start, &s_frag_id);
        if (regVert < 0 || regFrag < 0 || !s_vert_id || !s_frag_id)
        {
            vita_log::error("[DirectPresenter] Shader registration failed (vert=0x%X frag=0x%X)", regVert, regFrag);
            vita_dp_fini();
            return -1;
        }

        SceGxmVertexAttribute attrs[2];
        SceGxmVertexStream stream;

        // These names are part of libvita2d's texture shader ABI.
        const SceGxmProgramParameter* param_position = sceGxmProgramFindParameterByName(&texture_v_gxp_start, "aPosition");
        const SceGxmProgramParameter* param_texcoord = sceGxmProgramFindParameterByName(&texture_v_gxp_start, "aTexcoord");
        s_wvp_param                                  = sceGxmProgramFindParameterByName(&texture_v_gxp_start, "wvp");

        if (!param_position || !param_texcoord || !s_wvp_param)
        {
            vita_log::error("[DirectPresenter] Shader parameter lookup failed (aPosition=%p aTexcoord=%p wvp=%p)",
                param_position, param_texcoord, s_wvp_param);
            vita_dp_fini();
            return -1;
        }

        attrs[0].streamIndex    = 0;
        attrs[0].offset         = 0;
        attrs[0].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        attrs[0].componentCount = 3;
        attrs[0].regIndex       = sceGxmProgramParameterGetResourceIndex(param_position);

        attrs[1].streamIndex    = 0;
        attrs[1].offset         = 12;
        attrs[1].format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        attrs[1].componentCount = 2;
        attrs[1].regIndex       = sceGxmProgramParameterGetResourceIndex(param_texcoord);

        stream.stride      = sizeof(Vita2dTextureVertex);
        stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

        int vpRes = sceGxmShaderPatcherCreateVertexProgram(
            win->shader_patcher, s_vert_id, attrs, 2, &stream, 1, &s_vert_prog);

        int fpRes = sceGxmShaderPatcherCreateFragmentProgram(
            win->shader_patcher, s_frag_id,
            SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            win->msaa, NULL, &texture_v_gxp_start, &s_frag_prog);

        if (vpRes < 0 || fpRes < 0 || !s_vert_prog || !s_frag_prog)
        {
            vita_log::error("[DirectPresenter] Program creation failed (vert=0x%X frag=0x%X)", vpRes, fpRes);
            vita_dp_fini();
            return -1;
        }

        s_vertices = static_cast<Vita2dTextureVertex*>(gpu_alloc(
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
            sizeof(Vita2dTextureVertex) * 4, 4,
            SCE_GXM_MEMORY_ATTRIB_READ, &s_vertices_uid));

        s_indices = static_cast<uint16_t*>(gpu_alloc(
            SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
            sizeof(uint16_t) * 4, 2,
            SCE_GXM_MEMORY_ATTRIB_READ, &s_indices_uid));

        if (!s_vertices || !s_indices)
        {
            vita_log::error("[DirectPresenter] GPU buffer allocation failed (verts=%p idx=%p)", s_vertices, s_indices);
            vita_dp_fini();
            return -1;
        }

        s_indices[0] = 0;
        s_indices[1] = 1;
        s_indices[2] = 2;
        s_indices[3] = 3;

        s_dp_active = true;
        vita_log::info("[DirectPresenter] Initialized native GXM low-latency video presenter");
        return 0;
    }

    void vita_dp_fini(void)
    {
        s_dp_active = false;

        NVGXMwindow* win = gxmGetWindow();
        if (win && win->shader_patcher)
        {
            if (s_vert_prog)
                sceGxmShaderPatcherReleaseVertexProgram(win->shader_patcher, s_vert_prog);
            if (s_frag_prog)
                sceGxmShaderPatcherReleaseFragmentProgram(win->shader_patcher, s_frag_prog);
            if (s_vert_id)
                sceGxmShaderPatcherUnregisterProgram(win->shader_patcher, s_vert_id);
            if (s_frag_id)
                sceGxmShaderPatcherUnregisterProgram(win->shader_patcher, s_frag_id);
        }
        s_vert_prog = nullptr;
        s_frag_prog = nullptr;
        s_vert_id   = nullptr;
        s_frag_id   = nullptr;
        s_wvp_param = nullptr;

        if (s_vertices_uid >= 0)
        {
            gpu_free(s_vertices_uid);
            s_vertices_uid = -1;
            s_vertices     = nullptr;
        }
        if (s_indices_uid >= 0)
        {
            gpu_free(s_indices_uid);
            s_indices_uid = -1;
            s_indices     = nullptr;
        }

        vita_log::info("[DirectPresenter] Finalized");
    }

    int vita_dp_present_frame(void)
    {
        if (!s_dp_active)
            return -1;
        if (g_stats.frames_decoded == 0)
            return -1;

        int displayIdx = __atomic_load_n(&frame_display_idx, __ATOMIC_ACQUIRE);

        const GxmTexture* tex = __atomic_load_n(&frame_textures[displayIdx], __ATOMIC_ACQUIRE);
        if (!tex)
            return -1;

        SceGxmTexture gxmTexSnapshot = tex->gxm_tex;

        if (!sceGxmTextureGetData(&gxmTexSnapshot))
            return -1;

        float dw = 960.0f;
        float dh = 544.0f;
        float ox = 0.0f;
        float oy = 0.0f;

        if (image_scaling.enabled && !video_fullscreen_stretch)
        {
            dw = image_scaling.display_width;
            dh = image_scaling.display_height;
            ox = image_scaling.offset_x;
            oy = image_scaling.offset_y;
        }

        const uint32_t storageWidth  = gxm_texture_get_storage_width(tex);
        const uint32_t storageHeight = gxm_texture_get_storage_height(tex);
        const float uMax             = storageWidth > 0 ? (float)gxm_texture_get_width(tex) / storageWidth : 1.0f;
        const float vMax             = storageHeight > 0 ? (float)gxm_texture_get_height(tex) / storageHeight : 1.0f;

        s_vertices[0] = { ox, oy, 0.5f, 0.0f, 0.0f };
        s_vertices[1] = { ox + dw, oy, 0.5f, uMax, 0.0f };
        s_vertices[2] = { ox, oy + dh, 0.5f, 0.0f, vMax };
        s_vertices[3] = { ox + dw, oy + dh, 0.5f, uMax, vMax };

        NVGXMwindow* win = gxmGetWindow();
        if (!win || !win->context)
            return -1;

        // NanoVG restores its own culling state when it flushes the UI above this quad.
        sceGxmSetCullMode(win->context, SCE_GXM_CULL_NONE);
        sceGxmSetTwoSidedEnable(win->context, SCE_GXM_TWO_SIDED_DISABLED);

        // The video is a background blit and must not occlude NanoVG through depth writes.
        sceGxmSetFrontDepthFunc(win->context, SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(win->context, SCE_GXM_DEPTH_WRITE_DISABLED);

        sceGxmSetVertexProgram(win->context, s_vert_prog);
        sceGxmSetFragmentProgram(win->context, s_frag_prog);

        float ortho_matrix[16];
        matrix_ortho(ortho_matrix, 0.0f, 960.0f, 544.0f, 0.0f, -1.0f, 1.0f);
        void* wvp_buffer = nullptr;
        sceGxmReserveVertexDefaultUniformBuffer(win->context, &wvp_buffer);
        sceGxmSetUniformDataF(wvp_buffer, s_wvp_param, 0, 16, ortho_matrix);

        sceGxmSetFragmentTexture(win->context, 0, &gxmTexSnapshot);

        sceGxmSetVertexStream(win->context, 0, s_vertices);
        sceGxmDraw(win->context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, s_indices, 4);

        g_stats.frames_presented++;
        // FFmpeg releases deferred CDRAM buffers from the presentation side.
        VitaVideoRenderer::instance().onFramePresented(sceGxmTextureGetData(&gxmTexSnapshot));

        return 0;
    }

    bool vita_dp_is_active(void)
    {
        return s_dp_active;
    }

} // extern "C"
