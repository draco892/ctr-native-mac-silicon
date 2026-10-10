#include <platform/native_scene_geometry.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at %d\n",#c,__LINE__); return 1; } } while(0)
static int CheckOutput(const struct NativeDrawTriangle *tri,size_t n,const struct NativeSceneCamera *c)
{
    for(size_t i=0;i<n;i++) {
        if(tri[i].signedArea<=0 || !tri[i].source.textured || tri[i].source.texture.tpage!=23 || tri[i].source.texture.clut!=47 || tri[i].orderingBias!=-7) return 0;
        for(unsigned k=0;k<3;k++) {
            if(tri[i].depth[k]<c->nearDepth || tri[i].depth[k]>c->farDepth ||
               tri[i].screen[k][0]<0 || tri[i].screen[k][0]>=(s32)c->width ||
               tri[i].screen[k][1]<0 || tri[i].screen[k][1]>=(s32)c->height) return 0;
        }
    }
    return 1;
}
struct SubdivisionStats { size_t count; s64 area; };
static enum NativeAssetResult SubdivisionSink(void *user,const struct NativeDrawTriangle *t)
{
    struct SubdivisionStats *stats=user;
    if(t->signedArea<=0) return NATIVE_ASSET_INVALID_DATA;
    stats->count++; stats->area+=t->signedArea; return NATIVE_ASSET_OK;
}
static enum NativeAssetResult FailSink(void *user,const struct NativeDrawTriangle *t)
{ (void)t; size_t *count=user; return ++*count==3 ? NATIVE_ASSET_OUTPUT_TOO_SMALL : NATIVE_ASSET_OK; }
int main(void)
{
    struct NativeSceneCamera c; const s32 pos[3]={0}; const s16 rot[3]={0};
    CHECK(NativeSceneCamera_Init(pos,rot,512,512,128,128,1024,&c)==NATIVE_ASSET_OK);
    struct NativeModelTriangle m={.textured=1,.colors={0xff000000,0xff000080,0xff000000},.texture={.u={0,64,128},.v={0,64,128},.tpage=23,.clut=47}};
    struct NativeSceneClipVertex v[3]={{{-32,-32,64},{0},{0}},{{32,-32,192},{0},{0}},{{0,32,192},{0},{0}}};
    for(unsigned k=0;k<3;k++) NativeSceneGeometry_Attributes(&m,k,&v[k]);
    struct NativeSceneClipVertex saved[3]; memcpy(saved,v,sizeof(v));
    struct NativeDrawTriangle out[NATIVE_SCENE_CLIP_MAX_TRIANGLES]; size_t n;
    CHECK(NativeSceneGeometry_NeedsClip(&c,v));
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==2 && CheckOutput(out,n,&c));
    CHECK(memcmp(v,saved,sizeof(v))==0);
    int halfUv=0,halfColor=0;
    for(size_t i=0;i<n;i++) for(unsigned k=0;k<3;k++) if(out[i].depth[k]==128) {
        if(out[i].source.texture.u[k]==32 && out[i].source.texture.v[k]==32) halfUv=1;
        if((out[i].source.colors[k]&255)==64) halfColor=1;
    }
    CHECK(halfUv && halfColor);
    v[1].view[2]=64;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==1 && CheckOutput(out,n,&c));
    v[2].view[2]=-64;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==0);
    memcpy(v,saved,sizeof(v)); v[0].view[2]=1536; v[1].view[2]=512; v[2].view[2]=512;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==2 && CheckOutput(out,n,&c));
    for(unsigned k=0;k<3;k++) v[k].view[2]=1536;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==0);
    memcpy(v,saved,sizeof(v)); for(unsigned k=0;k<3;k++) v[k].view[2]=256;
    CHECK(!NativeSceneGeometry_NeedsClip(&c,v));
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==1 && CheckOutput(out,n,&c));
    struct NativeSceneClipVertex planar[3]={{{-128,-128,256},{0},{0}},{{128,-128,256},{0},{0}},{{0,128,256},{0},{0}}};
    for(unsigned k=0;k<3;k++) NativeSceneGeometry_Attributes(&m,k,&planar[k]);
    for(u32 depth=0;depth<=3;depth++) {
        struct SubdivisionStats subdiv={0}; size_t preflight;
        CHECK(NativeSceneGeometry_Subdivide(&c,&m,planar,-7,depth,NULL,NULL,&preflight)==NATIVE_ASSET_OK);
        CHECK(NativeSceneGeometry_Subdivide(&c,&m,planar,-7,depth,SubdivisionSink,&subdiv,&n)==NATIVE_ASSET_OK);
        CHECK(n==((size_t)1<<(2*depth)) && n==preflight && subdiv.count==n && subdiv.area==16384);
    }
    size_t published=0;
    CHECK(NativeSceneGeometry_Subdivide(&c,&m,planar,-7,2,FailSink,&published,&n)==NATIVE_ASSET_OUTPUT_TOO_SMALL && n==0 && published==3);
    CHECK(NativeSceneGeometry_Subdivide(&c,&m,planar,-7,4,NULL,NULL,&n)==NATIVE_ASSET_INVALID_ARGUMENT && n==0);
    // A triangle surrounding the entire viewport exercises all four side planes.
    v[0].view[0]=-2000; v[0].view[1]=-2000; v[1].view[0]=2000; v[1].view[1]=-2000; v[2].view[0]=0; v[2].view[1]=4000;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n>=2 && n<=7 && CheckOutput(out,n,&c));
    for(unsigned k=0;k<3;k++) v[k].view[0]=2000;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_OK && n==0);
    v[0].view[0]=INT32_MIN;
    CHECK(NativeSceneGeometry_Clip(&c,&m,v,-7,out,&n)==NATIVE_ASSET_INVALID_DATA && n==0);
    CHECK(NativeSceneGeometry_Clip(NULL,&m,v,-7,out,&n)==NATIVE_ASSET_INVALID_ARGUMENT && n==0);
    struct NativeProjectionConfig p={.rotation={.m={{4096,0,0},{0,4096,0},{0,0,4096}}},.translation={0,0,1024}};
    struct NativePackedModelVertex packed={.xy=(u16)-20|((u32)(u16)12<<16),.z=(u16)-8}; s32 view[3];
    CHECK(NativeSceneGeometry_Transform(&p,&packed,4,view)==NATIVE_ASSET_OK && view[0]==-5 && view[1]==3 && view[2]==254);
    CHECK(NativeSceneGeometry_Transform(&p,&packed,0,view)==NATIVE_ASSET_INVALID_ARGUMENT);
    puts("Native near/far/viewport clipping, winding, interpolated UV/RGB and scaled model transforms OK"); return 0;
}
