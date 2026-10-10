#include <platform/native_vertex_animation.h>
#include <string.h>
static const s16 VertexAnimation_Trig[1024][2]={
#include "native_trig_table.inc"
};
static s32 VertexAnimation_S16(u16 n) { return n<=INT16_MAX ? (s32)n : (s32)n-65536; }
static s64 VertexAnimation_Floor(s64 n,s64 d) { return n/d-(n%d<0); }
static int VertexAnimation_Resolve(const struct NativePtrMapView *map,u32 slot,size_t bytes,int optional,const u8 **out)
{
    void *p=NULL; *out=NULL;
    enum NativePtrMapResult result=NativePtrMap_Resolve(map,slot,bytes,&p);
    if(result==NATIVE_PTRMAP_OK) { *out=p; return 1; }
    return optional && result==NATIVE_PTRMAP_SLOT_NOT_FOUND && slot<=map->originSize && map->originSize-slot>=4 && CTR_ReadU32LE(map->origin+slot)==0;
}
static int VertexAnimation_Index(const struct NativeVertexAnimationView *a,u32 slot,u32 *index)
{
    const u8 *vertex;
    if(!VertexAnimation_Resolve(a->map,slot,NATIVE_VERTEX_BYTES,0,&vertex)) return 0;
    uintptr_t begin=(uintptr_t)a->mesh.vertices,target=(uintptr_t)vertex;
    if(target<begin || (target-begin)%NATIVE_VERTEX_BYTES || (target-begin)/NATIVE_VERTEX_BYTES>=a->mesh.vertexCount) return 0;
    *index=(u32)((target-begin)/NATIVE_VERTEX_BYTES); return 1;
}
enum NativeAssetResult NativeVertexAnimation_Open(const struct NativeLevelView *level,
    const struct NativeMeshView *mesh,struct NativeVertexAnimationView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(level==NULL || level->map==NULL || mesh==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    const struct NativePtrMapView *map=level->map;
    if(map->origin==NULL || map->originSize<NATIVE_LEVEL_BYTES || map->originSize>UINT32_MAX) return NATIVE_ASSET_INVALID_DATA;
    struct NativeMeshView checked; enum NativeAssetResult status=NativeLevel_GetMesh(level,&checked);
    if(status!=NATIVE_ASSET_OK) return status;
    if(mesh->vertices!=checked.vertices || mesh->vertexCount!=checked.vertexCount || mesh->quads!=checked.quads || mesh->bsp!=checked.bsp) return NATIVE_ASSET_INVALID_ARGUMENT;
    struct NativeVertexAnimationView a={.mesh=*mesh,.map=map,.scenery=(CTR_ReadU32LE(map->origin+0xdc)&4)!=0};
    u32 countSlot=a.scenery ? 0x174 : 0x34,tableSlot=a.scenery ? 0x178 : 0x38,maskSlot=a.scenery ? 0x170 : 0x28;
    a.count=CTR_ReadU32LE(map->origin+countSlot); size_t stride=a.scenery ? 16 : 8;
    if(a.count>INT32_MAX || a.count>map->originSize/stride) return NATIVE_ASSET_INVALID_DATA;
    if(a.count) {
        size_t bytes=((size_t)a.count+31)/32*4;
        if(!VertexAnimation_Resolve(map,tableSlot,(size_t)a.count*stride,0,&a.records) ||
           !VertexAnimation_Resolve(map,maskSlot,bytes,1,&a.visibility)) return NATIVE_ASSET_INVALID_DATA;
        if(!a.scenery && !VertexAnimation_Resolve(map,0x44,12,0,&a.environment)) return NATIVE_ASSET_INVALID_DATA;
        for(u32 i=0;i<a.count;i++) {
            u32 slot=(u32)(a.records-map->origin)+(u32)(i*stride),index;
            if(!VertexAnimation_Index(&a,slot,&index)) return NATIVE_ASSET_INVALID_DATA;
            if(!a.scenery) { const u8 *samples; if(!VertexAnimation_Resolve(map,slot+4,56,0,&samples)) return NATIVE_ASSET_INVALID_DATA; }
        }
    }
    *out=a; return NATIVE_ASSET_OK;
}
static u32 VertexAnimation_DepthCue(u32 rgb,const s32 farColor[3],s32 alpha)
{
    u32 out=rgb&0xff000000u;
    for(unsigned k=0;k<3;k++) {
        s32 color=(s32)((rgb>>(8*k))&255),delta=farColor[k]-color*16;
        if(delta<-32768) delta=-32768; if(delta>32767) delta=32767;
        s64 value=VertexAnimation_Floor((s64)color*65536+(s64)alpha*delta,65536);
        if(value<0) value=0; if(value>255) value=255;
        out|=(u32)value<<(8*k);
    }
    return out;
}
static void VertexAnimation_SinCos(u32 angle,s32 *s,s32 *c)
{
    s32 sine=VertexAnimation_Trig[angle&1023][0],cosine=VertexAnimation_Trig[angle&1023][1];
    switch((angle>>10)&3) {
    case 0: *s=sine; *c=cosine; break;
    case 1: *s=cosine; *c=-sine; break;
    case 2: *s=-sine; *c=-cosine; break;
    default: *s=-cosine; *c=sine; break;
    }
}
static int VertexAnimation_Overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y) return 1;
    return an && bn && x<y+bn && y<x+an;
}
enum NativeAssetResult NativeVertexAnimation_Apply(const struct NativeVertexAnimationView *a,u32 tick,
    const void *const *masks,const size_t *maskBytes,size_t maskCount,void *buffer,size_t capacity,struct NativeMeshView *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT; memset(out,0,sizeof(*out));
    if(a==NULL || a->map==NULL || maskCount>4 || (maskCount && (masks==NULL || maskBytes==NULL))) return NATIVE_ASSET_INVALID_ARGUMENT;
    size_t requiredMask=((size_t)a->count+31)/32*4;
    for(size_t k=0;k<maskCount;k++) if(masks[k]==NULL || maskBytes[k]<requiredMask) return NATIVE_ASSET_INVALID_DATA;
    if(!a->count) { *out=a->mesh; return NATIVE_ASSET_OK; }
#if SIZE_MAX <= UINT32_MAX
    if(a->mesh.vertexCount>SIZE_MAX/NATIVE_VERTEX_BYTES) return NATIVE_ASSET_INVALID_DATA;
#endif
    size_t bytes=(size_t)a->mesh.vertexCount*NATIVE_VERTEX_BYTES;
    if(buffer==NULL || capacity<bytes) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    if(VertexAnimation_Overlap(buffer,bytes,a->map->origin,a->map->originSize) ||
       VertexAnimation_Overlap(buffer,bytes,a->map->entries,a->map->count*sizeof(*a->map->entries)) ||
       VertexAnimation_Overlap(buffer,bytes,a,sizeof(*a)) || VertexAnimation_Overlap(buffer,bytes,out,sizeof(*out))) return NATIVE_ASSET_INVALID_ARGUMENT;
    for(size_t k=0;k<maskCount;k++) if(VertexAnimation_Overlap(buffer,bytes,masks[k],requiredMask)) return NATIVE_ASSET_INVALID_ARGUMENT;
    memcpy(buffer,a->mesh.vertices,bytes);
    for(u32 i=0;i<a->count;i++) {
        size_t word=(size_t)(i/32)*4; u32 bit=1u<<(i%32),visibility=0;
        if(maskCount) { for(size_t k=0;k<maskCount;k++) visibility|=CTR_ReadU32LE((const u8 *)masks[k]+word); }
        else visibility=a->visibility!=NULL ? CTR_ReadU32LE(a->visibility+word) : UINT32_MAX;
        if(!(visibility&bit)) continue;
        size_t stride=a->scenery ? 16 : 8; u32 slot=(u32)(a->records-a->map->origin)+(u32)(i*stride),index;
        if(!VertexAnimation_Index(a,slot,&index)) return NATIVE_ASSET_INVALID_DATA;
        u8 *vertex=(u8 *)buffer+(size_t)index*NATIVE_VERTEX_BYTES; const u8 *record=a->records+i*stride;
        if(a->scenery) {
            u32 xy=CTR_ReadU32LE(record+4),zw=CTR_ReadU32LE(record+8); s32 flags=VertexAnimation_S16((u16)(zw>>16)),sine,cosine;
            VertexAnimation_SinCos((u32)(flags&0x3fff)+(tick<<7),&sine,&cosine);
            if(flags<0) {
                u32 animated=xy+(u32)VertexAnimation_Floor(sine,128);
                CTR_WriteU32LE(vertex,animated|(xy&0xffff0000u));
                CTR_WriteU16LE(vertex+4,(u16)(zw+(u32)VertexAnimation_Floor(cosine,128)));
            }
            if(flags&0x4000) {
                const s32 black[3]={0}; u32 color=VertexAnimation_DepthCue(CTR_ReadU32LE(record+12),black,(sine+4096)/4);
                CTR_WriteU32LE(vertex+8,color); CTR_WriteU32LE(vertex+12,color);
            }
        } else {
            const u8 *samples; if(!VertexAnimation_Resolve(a->map,slot+4,56,0,&samples)) return NATIVE_ASSET_INVALID_DATA;
            unsigned first=(tick>>3)%28,second=(first+1)%28;
            u16 aColor=CTR_ReadU16LE(samples+first*2),bColor=CTR_ReadU16LE(samples+second*2);
            u32 rgb=(aColor&0x3f)|((u32)(aColor&0xfc0)<<2)|((u32)(aColor&0xf000)<<8)|((u32)(aColor&0xf000)<<4);
            s32 farColor[3]={(bColor&0x3f)<<4,(bColor&0xfc0)>>2,((bColor&0xf000)>>4)|((bColor&0xf000)>>8)};
            u32 color=VertexAnimation_DepthCue(rgb,farColor,(s32)(tick&7)*512);
            CTR_WriteU16LE(vertex+12,(u16)(color+CTR_ReadU16LE(a->environment)));
            u32 gray=(color>>16)&255; CTR_WriteU32LE(vertex+8,gray|(gray<<8)|(gray<<16));
        }
    }
    *out=a->mesh; out->vertices=buffer; return NATIVE_ASSET_OK;
}
