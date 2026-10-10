#include <platform/native_scene_gpu.h>
#include <platform/native_gpu_links.h>
#include <string.h>
enum NativeAssetResult NativeSceneGpu_Init(struct NativeSceneGpuPacket *packets,size_t capacity,u32 *ot,size_t otCount,struct NativeSceneGpuSink *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(packets==NULL || ot==NULL || capacity==0 || otCount==0 || otCount>65536 ||
       capacity>SIZE_MAX/sizeof(*packets) || otCount>SIZE_MAX/sizeof(*ot) ||
       (uintptr_t)packets%_Alignof(struct NativeSceneGpuPacket) || (uintptr_t)ot%_Alignof(u32) ||
       !NativeGpuLinks_IsRegisteredHostRange(packets,capacity*sizeof(*packets)) || !NativeGpuLinks_IsRegisteredHostRange(ot,otCount*sizeof(*ot))) return NATIVE_ASSET_INVALID_ARGUMENT;
    *out=(struct NativeSceneGpuSink){.packets=packets,.ot=ot,.capacity=capacity,.otCount=otCount}; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeSceneGpu_Emit(void *user,const struct NativeDrawTriangle *triangle)
{
    struct NativeSceneGpuSink *sink=user;
    if(sink==NULL || triangle==NULL || sink->packets==NULL || sink->ot==NULL || sink->otCount==0) return NATIVE_ASSET_INVALID_ARGUMENT;
    if((unsigned)triangle->source.textureBlend>NATIVE_TEXTURE_BLEND_SEMI) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(sink->count>=sink->capacity) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    if(sink->otCount>65536 || sink->capacity>SIZE_MAX/sizeof(*sink->packets) ||
       !NativeGpuLinks_IsRegisteredHostRange(sink->packets,sink->capacity*sizeof(*sink->packets)) ||
       !NativeGpuLinks_IsRegisteredHostRange(sink->ot,sink->otCount*sizeof(*sink->ot))) return NATIVE_ASSET_INVALID_ARGUMENT;
    u16 depth=0; for(unsigned k=0;k<3;k++) if(triangle->depth[k]>depth) depth=triangle->depth[k];
    s32 at=(s32)(depth>>6)+triangle->orderingBias;
    if(at<0) at=0; if((size_t)at>=sink->otCount) at=(s32)sink->otCount-1;
    struct NativeSceneGpuPacket packet={0};
    u32 code=NativeMaterial_TriangleCode(triangle->source.textured,triangle->source.texture.tpage,triangle->source.textureBlend);
    for(unsigned k=0;k<3;k++) {
        unsigned colorWord=triangle->source.textured ? 1+k*3 : 1+k*2;
        packet.words[colorWord]=(triangle->source.colors[k]&0xffffffu)|(k==0 ? code<<24 : 0);
        packet.words[colorWord+1]=(u16)triangle->screen[k][0]|((u32)(u16)triangle->screen[k][1]<<16);
        if(triangle->source.textured) packet.words[colorWord+2]=triangle->source.texture.u[k]|((u32)triangle->source.texture.v[k]<<8)|
            (k==0 ? (u32)triangle->source.texture.clut<<16 : k==1 ? (u32)triangle->source.texture.tpage<<16 : 0);
    }
    struct NativeSceneGpuPacket *destination=&sink->packets[sink->count];
    u32 token=NativeGpuLinks_FromHostPointer(destination);
    if(token==NATIVE_GPU_LINK_TERMINATOR) return NATIVE_ASSET_INVALID_ARGUMENT;
    packet.words[0]=(sink->ot[at]&0xffffffu)|((triangle->source.textured ? 9u : 6u)<<24);
    *destination=packet; sink->ot[at]=token; sink->count++;
    return NATIVE_ASSET_OK;
}
