#include <platform/native_mesh_geometry.h>
#include <string.h>
static u16 Mesh_U16(const u8 *p) { return (u16)((u16)p[0]|((u16)p[1]<<8)); }
static s16 Mesh_S16(const u8 *p) { u16 n=Mesh_U16(p); return (s16)(n<=INT16_MAX ? (s32)n : (s32)n-65536); }
enum NativeAssetResult NativeMesh_GetVertex(const struct NativeMeshView *mesh,u32 index,struct NativeMeshVertex *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(mesh==NULL || (mesh->vertexCount && mesh->vertices==NULL)) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(index>=mesh->vertexCount) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
    const u8 *wire=mesh->vertices+(size_t)index*NATIVE_VERTEX_BYTES;
    for(unsigned k=0;k<3;k++) out->position[k]=Mesh_S16(wire+2*k);
    out->flags=Mesh_U16(wire+6); out->colorHigh=CTR_ReadU32LE(wire+8); out->colorLow=CTR_ReadU32LE(wire+12);
    return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeMesh_GetQuad(const struct NativeMeshView *mesh,u32 index,struct NativeMeshQuad *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(mesh==NULL || (mesh->quadCount && mesh->quads==NULL) || (mesh->vertexCount && mesh->vertices==NULL)) return NATIVE_ASSET_INVALID_ARGUMENT;
    if(index>=mesh->quadCount) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
    const u8 *wire=mesh->quads+(size_t)index*NATIVE_QUAD_BYTES;
    struct NativeMeshQuad quad={0};
    for(unsigned k=0;k<9;k++) {
        quad.indices[k]=Mesh_U16(wire+2*k);
        if(quad.indices[k]>=mesh->vertexCount) return NATIVE_ASSET_INVALID_DATA;
    }
    quad.flags=Mesh_U16(wire+0x12); quad.drawOrderLow=CTR_ReadU32LE(wire+0x14); quad.drawOrderHigh=CTR_ReadU32LE(wire+0x18);
    *out=quad; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeMesh_GetLowTriangle(const struct NativeMeshView *mesh,u32 quadIndex,u32 triangleIndex,struct NativeMeshTriangle *out)
{
    static const u8 slots[2][3]={{2,0,3},{0,1,3}};
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(triangleIndex>=2) return NATIVE_ASSET_INDEX_OUT_OF_RANGE;
    struct NativeMeshQuad quad; struct NativeMeshTriangle triangle={0};
    enum NativeAssetResult status=NativeMesh_GetQuad(mesh,quadIndex,&quad);
    if(status!=NATIVE_ASSET_OK) return status;
    for(unsigned k=0;k<3;k++) {
        triangle.indices[k]=quad.indices[slots[triangleIndex][k]];
        status=NativeMesh_GetVertex(mesh,triangle.indices[k],&triangle.vertices[k]);
        if(status!=NATIVE_ASSET_OK) return status;
    }
    *out=triangle; return NATIVE_ASSET_OK;
}
