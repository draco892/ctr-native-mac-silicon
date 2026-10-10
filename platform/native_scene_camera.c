#include <platform/native_scene_camera.h>
#include <string.h>
static s64 Camera_Floor(s64 n,s64 d) { return n/d-(n%d<0); }
int NativeSceneCamera_IsValid(const struct NativeSceneCamera *c)
{ return c!=NULL && c->width>0 && c->width<=4096 && c->height>0 && c->height<=4096 && c->transform.h>0 && c->nearDepth>0 && c->farDepth>c->nearDepth && c->farDepth<=65535 && c->subdivisionDepth<=3; }
enum NativeAssetResult NativeSceneCamera_Init(const s32 position[3],const s16 angles[3],u32 width,u32 height,u16 h,s32 nearDepth,s32 farDepth,struct NativeSceneCamera *out)
{
    if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    struct NativeSceneCamera c={.width=width,.height=height,.nearDepth=nearDepth,.farDepth=farDepth};
    c.transform.h=h;
    if(position==NULL || angles==NULL || !NativeSceneCamera_IsValid(&c)) return NATIVE_ASSET_INVALID_ARGUMENT;
    struct NativeModelMatrix world;
    enum NativeAssetResult status=NativeInstance_Rotation(angles,&world); if(status!=NATIVE_ASSET_OK) return status;
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) c.transform.view.m[row][col]=world.m[col][row];
    memcpy(c.transform.position,position,sizeof(c.transform.position));
    c.transform.offset[0]=(s32)(width*32768u); c.transform.offset[1]=(s32)(height*32768u);
    *out=c; return NATIVE_ASSET_OK;
}
int NativeSceneCamera_BoxVisible(const struct NativeSceneCamera *c,const s16 minimum[3],const s16 maximum[3])
{
    if(!NativeSceneCamera_IsValid(c) || minimum==NULL || maximum==NULL) return 0;
    for(unsigned k=0;k<3;k++) if(minimum[k]>maximum[k]) return 0;
    unsigned outside[6]={0};
    s64 xOffset=Camera_Floor(c->transform.offset[0],65536),yOffset=Camera_Floor(c->transform.offset[1],65536);
    for(unsigned corner=0;corner<8;corner++) {
        s64 view[3];
        for(unsigned row=0;row<3;row++) {
            s64 dot=0;
            for(unsigned k=0;k<3;k++) dot+=(s64)c->transform.view.m[row][k]*((s64)((corner&(1u<<k)) ? maximum[k] : minimum[k])-c->transform.position[k]);
            view[row]=Camera_Floor(dot,4096);
        }
        outside[0]+=view[2]<c->nearDepth; outside[1]+=view[2]>c->farDepth;
        outside[2]+=view[0]*c->transform.h+xOffset*view[2]<0;
        outside[3]+=view[0]*c->transform.h-((s64)c->width-xOffset)*view[2]>0;
        outside[4]+=view[1]*c->transform.h+yOffset*view[2]<0;
        outside[5]+=view[1]*c->transform.h-((s64)c->height-yOffset)*view[2]>0;
    }
    for(unsigned plane=0;plane<6;plane++) if(outside[plane]==8) return 0;
    return 1;
}
enum NativeAssetResult NativeSceneCamera_ProjectTerrain(const struct NativeSceneCamera *camera,
    const struct NativeTerrainTriangle *source,struct NativeDrawTriangle *out,int *frontFacing)
{
    if(out!=NULL) memset(out,0,sizeof(*out)); if(frontFacing!=NULL) *frontFacing=0;
    if(out==NULL || frontFacing==NULL || source==NULL || !NativeSceneCamera_IsValid(camera)) return NATIVE_ASSET_INVALID_ARGUMENT;
    struct NativeProjectionConfig projection={.rotation=camera->transform.view,.h=camera->transform.h};
    memcpy(projection.offset,camera->transform.offset,sizeof(projection.offset));
    for(unsigned row=0;row<3;row++) {
        s64 dot=0; for(unsigned k=0;k<3;k++) dot-=(s64)projection.rotation.m[row][k]*camera->transform.position[k];
        s64 value=Camera_Floor(dot,4096);
        if(value<INT32_MIN || value>INT32_MAX) return NATIVE_ASSET_INVALID_ARGUMENT;
        projection.translation[row]=(s32)value;
    }
    struct NativePackedModelVertex packed[3]; struct NativeProjectionState state={0};
    struct NativeProjectionResult projected; struct NativeDrawTriangle triangle={0};
    for(unsigned k=0;k<3;k++) {
        const s16 *p=source->geometry.vertices[k].position;
        packed[k].xy=(u16)p[0]|((u32)(u16)p[1]<<16); packed[k].z=(u16)p[2];
        triangle.source.vertices[k]=source->geometry.indices[k]; triangle.source.colors[k]=source->geometry.vertices[k].colorHigh;
    }
    enum NativeAssetResult status=NativeModelProjection_Project3(&projection,packed,&state,&projected); if(status!=NATIVE_ASSET_OK) return status;
    memcpy(triangle.screen,state.screen,sizeof(triangle.screen));
    for(unsigned k=0;k<3;k++) triangle.depth[k]=state.depth[k+1];
    triangle.source.textured=source->textured; triangle.source.texture=source->texture; triangle.projectionFlags=projected.flags;
    triangle.orderingBias=source->orderingBias;
    s64 x1=(s64)triangle.screen[1][0]-triangle.screen[0][0],y1=(s64)triangle.screen[1][1]-triangle.screen[0][1];
    s64 x2=(s64)triangle.screen[2][0]-triangle.screen[0][0],y2=(s64)triangle.screen[2][1]-triangle.screen[0][1];
    triangle.signedArea=x1*y2-y1*x2;
    s64 determinant=0;
    for(unsigned k=0;k<3;k++) determinant+=(s64)projection.rotation.m[0][k]*
        ((s64)projection.rotation.m[1][(k+1)%3]*projection.rotation.m[2][(k+2)%3]-(s64)projection.rotation.m[1][(k+2)%3]*projection.rotation.m[2][(k+1)%3]);
    *frontFacing=NativeTerrain_FrontFacing(source,determinant<0 ? -triangle.signedArea : triangle.signedArea);
    triangle.averageDepth=(u16)(((u32)triangle.depth[0]+triangle.depth[1]+triangle.depth[2])/3);
    *out=triangle; return NATIVE_ASSET_OK;
}
