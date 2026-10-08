#include <platform/native_instance_transform.h>
#include <string.h>
static const s16 NativeInstance_Trig[1024][2]={
#include "native_trig_table.inc"
};
static void NativeInstance_SinCos(s16 angle,s16 *s,s16 *c)
{
    u32 a=(u16)angle;
    s16 sine=NativeInstance_Trig[a&1023][0],cosine=NativeInstance_Trig[a&1023][1];
    switch((a>>10)&3) {
    case 0: *s=sine; *c=cosine; break;
    case 1: *s=cosine; *c=(s16)-sine; break;
    case 2: *s=(s16)-sine; *c=(s16)-cosine; break;
    default: *s=(s16)-cosine; *c=sine; break;
    }
}
enum NativeAssetResult NativeInstance_Rotation(const s16 angles[3],struct NativeModelMatrix *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(angles==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    s16 sx,cx,sy,cy,sz,cz;
    NativeInstance_SinCos(angles[0],&sx,&cx); NativeInstance_SinCos(angles[1],&sy,&cy); NativeInstance_SinCos(angles[2],&sz,&cz);
    struct NativeModelMatrix y={.m={{cy,0,sy},{0,4096,0},{(s16)-sy,0,cy}}};
    if(angles[0]!=0) {
        const struct NativeModelMatrix x={.m={{4096,0,0},{0,cx,(s16)-sx},{0,sx,cx}}};
        struct NativeModelMatrix product; NativeModelMatrix_Compose(&y,&x,&product); y=product;
    }
    if(angles[2]!=0) {
        const struct NativeModelMatrix z={.m={{cz,(s16)-sz,0},{sz,cz,0},{0,0,4096}}};
        struct NativeModelMatrix product; NativeModelMatrix_Compose(&y,&z,&product); y=product;
    }
    *out=y; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeInstance_Projection(const struct NativeInstanceDefView *instance,
    u32 headerIndex,const struct NativeInstanceCamera *camera,struct NativeProjectionConfig *out,s32 *rawDepth)
{
    if(out!=NULL) memset(out,0,sizeof(*out)); if(rawDepth!=NULL) *rawDepth=0;
    if(instance==NULL || camera==NULL || out==NULL || rawDepth==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    struct NativeModelHeaderView header; struct NativeModelMatrix rotation,viewRotation;
    enum NativeAssetResult status=NativeModel_GetHeader(&instance->model,headerIndex,&header);
    if(status!=NATIVE_ASSET_OK) return status;
    NativeInstance_Rotation(instance->rotation,&rotation);
    int screenspace=(instance->flags&0x400u)!=0;
    struct NativeProjectionConfig result={.h=camera->h,.dqa=camera->dqa,.dqb=camera->dqb};
    memcpy(result.offset,camera->offset,sizeof(result.offset));
    const s32 position[3]={instance->position[0],instance->position[1],instance->position[2]}; s32 depth;
    status=NativeModelProjection_ViewTranslation(&camera->view,position,camera->position,screenspace,
        (instance->flags&0x8000000u)!=0,result.translation,&depth);
    if(status!=NATIVE_ASSET_OK) return status;
    status=NativeModelMatrix_Build(&rotation,header.scale,instance->scale,depth,(instance->flags&0x200u)!=0,&viewRotation);
    if(status!=NATIVE_ASSET_OK) return status;
    NativeModelMatrix_Compose(&camera->view,&viewRotation,&result.rotation);
    *rawDepth=depth; *out=result; return NATIVE_ASSET_OK;
}
