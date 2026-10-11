#ifndef PLATFORM_NATIVE_MATERIAL_H
#define PLATFORM_NATIVE_MATERIAL_H
#include <macros.h>
enum NativeTextureBlendPolicy {
    NATIVE_TEXTURE_BLEND_AUTO,
    NATIVE_TEXTURE_BLEND_OPAQUE,
    NATIVE_TEXTURE_BLEND_SEMI
};
// CTR's ordinary GT3 writers use page blend bits 3 as the opaque marker.
// An explicit policy can override it (including quarter-source blending).
// The same decision drives diagnostic sampling and GPU command publication.
static inline int NativeMaterial_IsSemiTransparent(u16 tpage, enum NativeTextureBlendPolicy policy)
{
    return policy==NATIVE_TEXTURE_BLEND_SEMI ||
        (policy==NATIVE_TEXTURE_BLEND_AUTO && (tpage&0x60u)!=0x60u);
}
static inline u8 NativeMaterial_TriangleCode(int textured, u16 tpage, enum NativeTextureBlendPolicy policy)
{
    return textured ? (NativeMaterial_IsSemiTransparent(tpage,policy) ? 0x36 : 0x34) : 0x30;
}
// RGB8 diagnostic equivalent of the existing native renderer's blend factors.
// Integer truncation and saturation are explicit. This is not GPU quantization,
// dithering or framebuffer-mask emulation.
static inline u8 NativeMaterial_BlendChannel(u8 background, u8 foreground, u16 tpage)
{
    u32 value;
    switch((tpage>>5)&3) {
    case 0: value=((u32)background+foreground)/2; break;
    case 1: value=(u32)background+foreground; break;
    case 2: value=background>foreground ? background-foreground : 0; break;
    default: value=background+(u32)foreground/4; break;
    }
    return (u8)(value>255 ? 255 : value);
}
#endif
