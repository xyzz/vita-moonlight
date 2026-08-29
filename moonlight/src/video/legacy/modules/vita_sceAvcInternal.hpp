#ifndef VITA_NONGAME_PROTOTYPES_H
#define VITA_NONGAME_PROTOTYPES_H

#include <psp2/types.h>
#include <psp2/videodec.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Internal Videodec functions available in libSceVideodec_stub.a
    int sceVideodecSetConfigInternal(SceVideodecType codec, int config);
    int sceVideodecInitLibraryInternal(SceVideodecType codec, const SceVideodecQueryInitInfoHwAvcdec* initInfo);

// Internal Avcdec functions available in libSceVideodec_stub.a (yes, Videodec stub contains Avcdec internal symbols)
#define SCE_AVCDEC_MODE_EXTENDED 2

    int sceAvcdecSetDecodeModeInternal(SceVideodecType codec, int mode);
    int sceAvcdecSetLowDelayModeNongameapp(SceVideodecType codec, int mode);
    int sceAvcdecQueryDecoderMemSizeInternal(SceVideodecType codec, const SceAvcdecQueryDecoderInfo* query, SceAvcdecDecoderInfo* decoderInfo);
    int sceAvcdecCreateDecoderInternal(SceVideodecType codec, SceAvcdecCtrl* decoder, const SceAvcdecQueryDecoderInfo* query);
    int sceAvcdecDecodeAuInternal(const SceAvcdecCtrl* decoder, const SceAvcdecAu* au, int* timeout);
    int sceAvcdecDecodeGetPictureInternal(const SceAvcdecCtrl* decoder, SceAvcdecArrayPicture* array_picture, int* timeout);

#ifdef __cplusplus
}
#endif

#define SCE_AVCDEC_DELAY_MODE_LOW 0

#endif // VITA_NONGAME_PROTOTYPES_H
