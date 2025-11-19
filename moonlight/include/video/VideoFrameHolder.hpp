// Limpio: solo declaraciones. Implementación en VideoFrameHolder.cpp
#pragma once
#ifndef __cplusplus
#error "VideoFrameHolder.hpp requiere C++"
#endif

#include <atomic>
#include <cstdint>
#include <mutex>

struct vita2d_texture;
struct SceGxmTexture;

struct GxmFrame
{
    const vita2d_texture* texture   = nullptr;
    const SceGxmTexture* gxmTexture = nullptr;
    uint32_t w                      = 0;
    uint32_t h                      = 0;
    uint64_t ptsMs                  = 0;
};

class VideoFrameHolder
{
  public:
    static VideoFrameHolder& instance();

    // Publica una textura ya residente en VRAM/GXM
    void pushTexture(const vita2d_texture* texture, uint32_t w, uint32_t h, uint64_t ptsMs);

    // Devuelve true si había frame nuevo; 'out' recibe referencia a la textura
    bool popLatest(GxmFrame& out);
    // Clear any pending frame and mark none available.
    void clear();

    uint64_t framesPushed() const { return framesPushed_.load(std::memory_order_relaxed); }
    uint64_t framesPopped() const { return framesPopped_.load(std::memory_order_relaxed); }

  private:
    VideoFrameHolder()                                   = default;
    VideoFrameHolder(const VideoFrameHolder&)            = delete;
    VideoFrameHolder& operator=(const VideoFrameHolder&) = delete;

    mutable std::mutex mutex_;
    GxmFrame latest_;
    std::atomic<bool> hasNew_ { false };
    std::atomic<uint64_t> framesPushed_ { 0 };
    std::atomic<uint64_t> framesPopped_ { 0 };
};
