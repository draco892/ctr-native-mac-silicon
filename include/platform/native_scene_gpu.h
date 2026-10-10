#ifndef PLATFORM_NATIVE_SCENE_GPU_H
#define PLATFORM_NATIVE_SCENE_GPU_H
#include <platform/native_scene_render.h>
struct NativeSceneGpuPacket { u32 words[10]; };
CTR_STATIC_ASSERT(sizeof(struct NativeSceneGpuPacket)==40);
struct NativeSceneGpuSink { struct NativeSceneGpuPacket *packets; u32 *ot; size_t capacity,count,otCount; };
// Existing native GPU token registry and retail-shaped G3/GT3 packets. Caller
// owns registered disjoint packet/OT ranges and initializes OT tags. No casts
// from LEV records, no host pointer stored in a tag. Init does not clear ranges.
enum NativeAssetResult NativeSceneGpu_Init(struct NativeSceneGpuPacket *packets,size_t capacity,u32 *ot,size_t otCount,struct NativeSceneGpuSink *out);
// Sink for terrain/model consumers. Max corner depth>>6 plus orderingBias,
// clamped OT index. GT3 chooses semi-transparency from tpage ABR as native level
// direct writers do. G3 is opaque. Each packet reserves 40 bytes, even G3.
enum NativeAssetResult NativeSceneGpu_Emit(void *user,const struct NativeDrawTriangle *triangle);
#endif
