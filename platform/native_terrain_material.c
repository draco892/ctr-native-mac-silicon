#include <platform/native_terrain_material.h>
#include <string.h>
static const u32 selectors[4]={0x00506478,0x5014788c,0x647828a0,0x788ca03c};
static const u32 modes[24]={
    0x18100800,0x80900818,0x00081018,0x98881000,0x98081000,0x00881018,0x80100818,0x18900800,
    0x10081898,0x88881810,0x18180810,0x90980888,0x90180888,0x18980810,0x88081810,0x10881898,
    0x08181090,0x98981008,0x10101808,0x88901898,0x88101898,0x10901808,0x98181008,0x08981090
};
static u16 Terrain_U16(const u8 *p) { return (u16)(p[0]|((u16)p[1]<<8)); }
static s16 Terrain_S16(const u8 *p) { u16 n=Terrain_U16(p); return (s16)(n<=INT16_MAX ? (s32)n : (s32)n-65536); }
static int Terrain_Span(const struct NativePtrMapView *map,u32 offset,size_t bytes)
{ return offset<=map->originSize && bytes<=map->originSize-offset; }
static enum NativeAssetResult Terrain_Texture(const struct NativePtrMapView *map,u32 slot,u32 lod,u32 tick,const u8 **out)
{
    void *pointer=NULL; *out=NULL;
    if(!Terrain_Span(map,slot,4)) return NATIVE_ASSET_INVALID_DATA;
    enum NativePtrMapResult result=NativePtrMap_Resolve(map,slot,0,&pointer);
    if(result==NATIVE_PTRMAP_SLOT_NOT_FOUND && CTR_ReadU32LE(map->origin+slot)==0) return NATIVE_ASSET_OK;
    if(result!=NATIVE_PTRMAP_OK) return NATIVE_ASSET_INVALID_DATA;
    u32 offset=(u32)((u8 *)pointer-map->origin);
    if(offset&1) {
        offset--;
        if(!Terrain_Span(map,offset,12)) return NATIVE_ASSET_INVALID_DATA;
        const u8 *animation=map->origin+offset;
        s32 count=Terrain_S16(animation+4),skip=Terrain_S16(animation+8);
        if(count<=0 || skip<0 || skip>31 || !Terrain_Span(map,offset+12,(size_t)count*4)) return NATIVE_ASSET_INVALID_DATA;
        s64 time=(s64)tick+Terrain_S16(animation+6),divisor=1LL<<skip;
        s64 frame=(time/divisor-(time%divisor<0))%count;
        if(frame<0) frame+=count;
        if(NativePtrMap_Resolve(map,offset+12+(u32)frame*4,48,&pointer)!=NATIVE_PTRMAP_OK) return NATIVE_ASSET_INVALID_DATA;
        offset=(u32)((u8 *)pointer-map->origin);
    }
    if(!Terrain_Span(map,offset,48)) return NATIVE_ASSET_INVALID_DATA;
    *out=map->origin+offset+lod*12;
    if(Terrain_U16(*out+6)&0xfe00u) { *out=NULL; return NATIVE_ASSET_INVALID_DATA; }
    return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeTerrain_GetTriangle(const struct NativeLevelView *level,
    const struct NativeMeshView *mesh,u32 quadIndex,u32 face,u32 triangle,u32 lod,u32 tick,
    struct NativeTerrainTriangle *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(level==NULL || level->map==NULL || level->map->origin==NULL || mesh==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(face>=4 || triangle>=2 || lod>=3) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
    struct NativeMeshQuad quad; struct NativeTerrainTriangle result={0};
    enum NativeAssetResult status=NativeMesh_GetQuad(mesh,quadIndex,&quad);
    if(status!=NATIVE_ASSET_OK) return status;
    u32 mode=(quad.drawOrderLow>>(8+face*5))&31;
    if(mode>=24) return NATIVE_ASSET_INVALID_DATA;
    result.selectorWord=modes[mode]; result.doubleSided=(quad.drawOrderLow>>31)!=0;
    u32 bias=(quad.drawOrderHigh>>(face*8))&255;
    result.orderingBias=(s16)(bias<128 ? (s32)bias : (s32)bias-256);
    u8 slots[4]; u8 uv[9][2]={{0}};
    for(unsigned k=0;k<4;k++) slots[k]=(u8)(((selectors[face]>>((result.selectorWord>>((3-k)*8))&31))&255)/20);
    static const u8 triangles[2][3]={{0,1,2},{1,3,2}};
    for(unsigned k=0;k<3;k++) result.geometry.indices[k]=quad.indices[slots[triangles[triangle][k]]];
    if(result.geometry.indices[0]==result.geometry.indices[1] || result.geometry.indices[1]==result.geometry.indices[2] || result.geometry.indices[0]==result.geometry.indices[2]) return NATIVE_ASSET_NOT_FOUND;
    const struct NativePtrMapView *map=level->map;
    // Mesh span must belong to this map; integer checks avoid pointer-subtract UB.
    uintptr_t base=(uintptr_t)map->origin,quadBase=(uintptr_t)mesh->quads;
    if(quadBase<base || quadBase-base>map->originSize) return NATIVE_ASSET_INVALID_ARGUMENT;
    size_t quadOffset=quadBase-base+(size_t)quadIndex*NATIVE_QUAD_BYTES;
    if(quadOffset>UINT32_MAX || !Terrain_Span(map,(u32)quadOffset,NATIVE_QUAD_BYTES)) return NATIVE_ASSET_INVALID_DATA;
    const u8 *texture;
    status=Terrain_Texture(map,(u32)quadOffset+0x1c+face*4,lod,tick,&texture);
    if(status!=NATIVE_ASSET_OK) return status;
    if(texture!=NULL) {
        const u8 offsets[4]={0,4,8,10}; const u8 normal[4]={0,1,2,3},flipped[4]={1,0,3,2};
        const u8 *order=(result.selectorWord&0x800000u) ? flipped : normal;
        for(unsigned k=0;k<4;k++) {
            if(k==3 && (result.selectorWord&0x80u)) continue;
            uv[slots[k]][0]=texture[offsets[order[k]]]; uv[slots[k]][1]=texture[offsets[order[k]]+1];
        }
        result.textured=1; result.texture.clut=Terrain_U16(texture+2); result.texture.tpage=Terrain_U16(texture+6);
    }
    for(unsigned k=0;k<3;k++) {
        status=NativeMesh_GetVertex(mesh,result.geometry.indices[k],&result.geometry.vertices[k]); if(status!=NATIVE_ASSET_OK) return status;
        result.texture.u[k]=uv[slots[triangles[triangle][k]]][0]; result.texture.v[k]=uv[slots[triangles[triangle][k]]][1];
    }
    *out=result; return NATIVE_ASSET_OK;
}
int NativeTerrain_FrontFacing(const struct NativeTerrainTriangle *triangle,s64 area)
{
    if(triangle==NULL || area==0) return 0;
    return triangle->doubleSided || ((area<0)==((triangle->selectorWord>>31)!=0));
}
