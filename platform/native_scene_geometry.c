#include <platform/native_scene_geometry.h>
#include <string.h>
static s64 SceneGeometry_Floor(s64 n,s64 d) { return n/d-(n%d<0); }
enum NativeAssetResult NativeSceneGeometry_Transform(const struct NativeProjectionConfig *p,
    const struct NativePackedModelVertex *v,unsigned scale,s32 view[3])
{
    if(p==NULL || v==NULL || view==NULL || (scale!=1 && scale!=4)) return NATIVE_ASSET_INVALID_ARGUMENT;
    s32 input[3]={(s16)v->xy,(s16)(v->xy>>16),(s16)v->z};
    for(unsigned row=0;row<3;row++) {
        s64 value=0;
        for(unsigned k=0;k<3;k++) value+=(s64)p->rotation.m[row][k]*input[k];
        value=SceneGeometry_Floor(SceneGeometry_Floor(value,4096)+p->translation[row],scale);
        if(value<-(1<<24) || value>(1<<24)) return NATIVE_ASSET_INVALID_DATA;
        view[row]=(s32)value;
    }
    return NATIVE_ASSET_OK;
}
void NativeSceneGeometry_Attributes(const struct NativeModelTriangle *m,unsigned k,struct NativeSceneClipVertex *v)
{
    v->uv[0]=(s32)m->texture.u[k]*65536; v->uv[1]=(s32)m->texture.v[k]*65536;
    for(unsigned channel=0;channel<3;channel++) v->rgb[channel]=(s32)((m->colors[k]>>(channel*8))&255)*65536;
}
static s64 SceneGeometry_Distance(const struct NativeSceneCamera *c,const struct NativeSceneClipVertex *v,unsigned plane)
{
    s64 z=v->view[2],x=SceneGeometry_Floor(c->transform.offset[0],65536),y=SceneGeometry_Floor(c->transform.offset[1],65536);
    switch(plane) {
    case 0: return z-c->nearDepth;
    case 1: return c->farDepth-z;
    case 2: return (s64)v->view[0]*c->transform.h+x*z;
    case 3: return ((s64)c->width-1-x)*z-(s64)v->view[0]*c->transform.h;
    case 4: return (s64)v->view[1]*c->transform.h+y*z;
    default: return ((s64)c->height-1-y)*z-(s64)v->view[1]*c->transform.h;
    }
}
int NativeSceneGeometry_NeedsClip(const struct NativeSceneCamera *c,const struct NativeSceneClipVertex v[3])
{
    if(!NativeSceneCamera_IsValid(c) || v==NULL) return 1;
    for(unsigned i=0;i<3;i++) for(unsigned k=0;k<3;k++) if(v[i].view[k]<-(1<<24) || v[i].view[k]>(1<<24)) return 1;
    for(unsigned i=0;i<3;i++) for(unsigned plane=0;plane<6;plane++)
        if(SceneGeometry_Distance(c,&v[i],plane)<0) return 1;
    return 0;
}
static s32 SceneGeometry_Lerp(s32 a,s32 b,s64 t) { return (s32)((s64)a+((s64)b-a)*t/65536); }
static struct NativeSceneClipVertex SceneGeometry_Intersection(const struct NativeSceneCamera *c,
    const struct NativeSceneClipVertex *a,const struct NativeSceneClipVertex *b,s64 da,s64 db,unsigned plane)
{
    s64 t=da*65536/(da-db);
    struct NativeSceneClipVertex v;
    for(unsigned k=0;k<3;k++) { v.view[k]=SceneGeometry_Lerp(a->view[k],b->view[k],t); v.rgb[k]=SceneGeometry_Lerp(a->rgb[k],b->rgb[k],t); }
    for(unsigned k=0;k<2;k++) v.uv[k]=SceneGeometry_Lerp(a->uv[k],b->uv[k],t);
    if(plane==0) v.view[2]=c->nearDepth;
    if(plane==1) v.view[2]=c->farDepth;
    return v;
}
enum NativeAssetResult NativeSceneGeometry_Clip(const struct NativeSceneCamera *c,
    const struct NativeModelTriangle *m,const struct NativeSceneClipVertex input[3],s16 bias,
    struct NativeDrawTriangle output[NATIVE_SCENE_CLIP_MAX_TRIANGLES],size_t *count)
{
    if(count!=NULL) *count=0;
    if(output!=NULL) memset(output,0,sizeof(*output)*NATIVE_SCENE_CLIP_MAX_TRIANGLES);
    if(count==NULL || output==NULL || input==NULL || m==NULL || !NativeSceneCamera_IsValid(c)) return NATIVE_ASSET_INVALID_ARGUMENT;
    for(unsigned i=0;i<3;i++) {
        for(unsigned k=0;k<3;k++) if(input[i].view[k]<-(1<<24) || input[i].view[k]>(1<<24) || input[i].rgb[k]<0 || input[i].rgb[k]>255*65536) return NATIVE_ASSET_INVALID_DATA;
        for(unsigned k=0;k<2;k++) if(input[i].uv[k]<0 || input[i].uv[k]>255*65536) return NATIVE_ASSET_INVALID_DATA;
    }
    struct NativeSceneClipVertex polygon[2][10]; memcpy(polygon[0],input,3*sizeof(*input));
    size_t size=3; unsigned buffer=0;
    for(unsigned plane=0;plane<6 && size;plane++) {
        size_t next=0;
        for(size_t i=0;i<size;i++) {
            const struct NativeSceneClipVertex *a=&polygon[buffer][i],*b=&polygon[buffer][(i+1)%size];
            s64 da=SceneGeometry_Distance(c,a,plane),db=SceneGeometry_Distance(c,b,plane);
            if(da>=0) { if(next>=10) return NATIVE_ASSET_INVALID_DATA; polygon[buffer^1][next++]=*a; }
            if((da<0 && db>0) || (da>0 && db<0)) { if(next>=10) return NATIVE_ASSET_INVALID_DATA; polygon[buffer^1][next++]=SceneGeometry_Intersection(c,a,b,da,db,plane); }
        }
        size=next; buffer^=1;
    }
    if(size<3) return NATIVE_ASSET_OK;
    if(size>9) return NATIVE_ASSET_INVALID_DATA;
    size_t written=0;
    for(size_t i=1;i+1<size;i++) {
        struct NativeDrawTriangle tri={.source=*m,.orderingBias=bias};
        const size_t indices[3]={0,i,i+1};
        for(unsigned k=0;k<3;k++) {
            const struct NativeSceneClipVertex *v=&polygon[buffer][indices[k]];
            for(unsigned axis=0;axis<2;axis++) {
                s64 coordinate=SceneGeometry_Floor(c->transform.offset[axis],65536)+(s64)v->view[axis]*c->transform.h/v->view[2];
                s64 limit=axis==0 ? c->width-1 : c->height-1;
                // Integer intersection rounding can move a boundary by one unit.
                if(coordinate<0) coordinate=0; if(coordinate>limit) coordinate=limit;
                tri.screen[k][axis]=(s16)coordinate;
            }
            tri.depth[k]=(u16)v->view[2];
            tri.source.texture.u[k]=(u8)((v->uv[0]+32768)/65536);
            tri.source.texture.v[k]=(u8)((v->uv[1]+32768)/65536);
            tri.source.colors[k]=m->colors[0]&0xff000000u;
            for(unsigned channel=0;channel<3;channel++) tri.source.colors[k]|=(u32)((v->rgb[channel]+32768)/65536)<<(8*channel);
        }
        tri.signedArea=((s64)tri.screen[1][0]-tri.screen[0][0])*(tri.screen[2][1]-tri.screen[0][1])-
            ((s64)tri.screen[2][0]-tri.screen[0][0])*(tri.screen[1][1]-tri.screen[0][1]);
        tri.averageDepth=(u16)(((u32)tri.depth[0]+tri.depth[1]+tri.depth[2])/3);
        if(tri.signedArea) output[written++]=tri;
    }
    *count=written; return NATIVE_ASSET_OK;
}

static struct NativeSceneClipVertex SceneGeometry_Midpoint(const struct NativeSceneClipVertex *a,const struct NativeSceneClipVertex *b)
{
    struct NativeSceneClipVertex v;
    for(unsigned k=0;k<3;k++) { v.view[k]=(s32)(((s64)a->view[k]+b->view[k])/2); v.rgb[k]=(a->rgb[k]+b->rgb[k])/2; }
    for(unsigned k=0;k<2;k++) v.uv[k]=(a->uv[k]+b->uv[k])/2;
    return v;
}
static enum NativeAssetResult SceneGeometry_Subdivide(const struct NativeSceneCamera *c,const struct NativeModelTriangle *m,
    const struct NativeSceneClipVertex v[3],s16 bias,u32 depth,NativeSceneGeometrySink sink,void *user,size_t *count)
{
    if(depth) {
        struct NativeSceneClipVertex mid[3];
        for(unsigned k=0;k<3;k++) mid[k]=SceneGeometry_Midpoint(&v[k],&v[(k+1)%3]);
        const struct NativeSceneClipVertex children[4][3]={{v[0],mid[0],mid[2]},{mid[0],v[1],mid[1]},{mid[2],mid[1],v[2]},{mid[0],mid[1],mid[2]}};
        for(unsigned k=0;k<4;k++) {
            enum NativeAssetResult status=SceneGeometry_Subdivide(c,m,children[k],bias,depth-1,sink,user,count);
            if(status!=NATIVE_ASSET_OK) return status;
        }
        return NATIVE_ASSET_OK;
    }
    struct NativeDrawTriangle clipped[NATIVE_SCENE_CLIP_MAX_TRIANGLES]; size_t n;
    enum NativeAssetResult status=NativeSceneGeometry_Clip(c,m,v,bias,clipped,&n);
    if(status!=NATIVE_ASSET_OK) return status;
    for(size_t i=0;i<n;i++) {
        if(sink!=NULL) { status=sink(user,&clipped[i]); if(status!=NATIVE_ASSET_OK) return status; }
        (*count)++;
    }
    return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeSceneGeometry_Subdivide(const struct NativeSceneCamera *c,const struct NativeModelTriangle *m,
    const struct NativeSceneClipVertex v[3],s16 bias,u32 depth,NativeSceneGeometrySink sink,void *user,size_t *count)
{
    if(count==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
    *count=0;
    if(depth>3) return NATIVE_ASSET_INVALID_ARGUMENT;
    // Validate all inputs before the first callback, including fully culled data.
    struct NativeDrawTriangle checked[NATIVE_SCENE_CLIP_MAX_TRIANGLES]; size_t n;
    enum NativeAssetResult status=NativeSceneGeometry_Clip(c,m,v,bias,checked,&n);
    if(status!=NATIVE_ASSET_OK || !n) return status;
    size_t emitted=0;
    status=SceneGeometry_Subdivide(c,m,v,bias,depth,sink,user,&emitted);
    if(status==NATIVE_ASSET_OK) *count=emitted;
    return status;
}
