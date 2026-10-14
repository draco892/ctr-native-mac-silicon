#include <platform/native_scene_snapshot.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
struct Output {struct NativeDrawTriangle triangles[32];unsigned bucket[32],count;};
static enum NativeAssetResult Save(void *user,unsigned bucket,const struct NativeDrawTriangle *triangle)
{
    struct Output *out=user;if(out->count==32) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    out->triangles[out->count]=*triangle;out->bucket[out->count++]=bucket;return NATIVE_ASSET_OK;
}
static void Capture(struct NativeSceneSnapshot *scene,uint64_t signature,uint64_t id,float x,float cameraZ,int slanted)
{
    NativeSceneSnapshot_Begin(scene,1,signature);
    struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={0,0,cameraZ}},
        .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
    NativeSceneSnapshot_Camera(scene,0,&camera);
    struct NativeSceneObjectSnapshot object={.id=id,.modelIdentity=9,.animation=1,.frame=(u32)x,.supported=1,
        .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={x,0,256}}};
    NativeSceneSnapshot_Object(scene,&object);
    struct NativeSceneSourceTriangle triangle={.object=id,.primitive=3,.material={.textured=1,.colors={0x808080,0x808080,0x808080},
        .vertices={0,1,2},.texture={.u={0,16,32},.v={0,16,32},.tpage=96,.clut=4}},
        .local={{-20,-20,0},{20,-20,0},{0,20,0}},.cull=0};
    if(slanted) {triangle.local[0][2]=-80;triangle.local[1][2]=triangle.local[2][2]=80;}
    NativeSceneSnapshot_Triangle(scene,&triangle);
    memset(&object,0,sizeof(object));memset(&triangle,0,sizeof(triangle)); /* Inputs can immediately disappear. */
    NativeSceneSnapshot_Seal(scene);
}
static int Render(struct NativeSceneSnapshot *scene,float alpha,struct Output *out,struct NativeSceneSnapshotStats *stats)
{
    memset(out,0,sizeof(*out));return NativeSceneSnapshot_Render(scene,0,alpha,Save,out,stats)==NATIVE_ASSET_OK;
}
int main(void)
{
    struct NativeSceneSnapshot *scene=NativeSceneSnapshot_Create();CHECK(scene);
    struct Output out;struct NativeSceneSnapshotStats stats;
    Capture(scene,1,1,-64,0,0);Capture(scene,1,1,64,0,0);
    for(unsigned i=0;i<=120;i++) {
        float alpha=(float)i/120;CHECK(Render(scene,alpha,&out,&stats) && out.count==1);
        float expected=96+(-64+128*alpha-20)*0.5f;
        CHECK(fabsf(out.triangles[0].visualXY[0][0]-expected)<0.51f);
        CHECK(stats.objects==1 && stats.sources==1 && stats.matched==1 && stats.valid);
    }
    CHECK(out.triangles[0].source.texture.clut==4 && out.triangles[0].source.texture.tpage==96);
    Capture(scene,1,2,64,0,0);CHECK(Render(scene,0,&out,&stats) && stats.matched==0);
    CHECK(out.triangles[0].screen[0][0]==118); /* Reused address gets a different generation ID. */
    Capture(scene,2,2,-64,0,0);CHECK(Render(scene,0,&out,&stats) && stats.matched==0 && out.triangles[0].screen[0][0]==54);
    NativeSceneSnapshot_Reset(scene);Capture(scene,3,1,0,0,1);Capture(scene,3,1,0,200,1);
    CHECK(Render(scene,0,&out,&stats) && out.count==1);unsigned oldBucket=out.bucket[0];
    CHECK(Render(scene,1,&out,&stats) && out.count>1 && stats.clipped==1 && out.bucket[0]<oldBucket);
    for(unsigned i=0;i<out.count;i++) for(unsigned k=0;k<3;k++) {
        CHECK(out.triangles[i].depth[k]>=32 && out.triangles[i].depth[k]<=1024);
        CHECK(out.triangles[i].visualXY[k][0]>=0 && out.triangles[i].visualXY[k][0]<=192);
        CHECK(out.triangles[i].visualXY[k][1]>=0 && out.triangles[i].visualXY[k][1]<=128);
    }
    /* The source may be invisible at the previous camera pose. Intermediate
     * visibility is rebuilt from the full source, not a previous draw list. */
    NativeSceneSnapshot_Reset(scene);Capture(scene,4,1,1000,0,0);Capture(scene,4,1,0,0,0);
    CHECK(Render(scene,0,&out,&stats) && out.count==0);
    CHECK(Render(scene,1,&out,&stats) && out.count==1);
    NativeSceneSnapshot_Begin(scene,0,4);memset(&out,0,sizeof(out));
    CHECK(NativeSceneSnapshot_Render(scene,0,0.5f,Save,&out,&stats)==NATIVE_ASSET_NOT_FOUND && !out.count);
    /* Reflected view matrices remain reflected while taking the short turn. */
    for(unsigned frame=0;frame<2;frame++) {
        NativeSceneSnapshot_Begin(scene,1,5);float angle=(frame?-179:179)*0.01745329252f;
        struct NativeSceneCameraSnapshot camera={.width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        camera.view.basis[0][0]=cosf(angle);camera.view.basis[0][1]=-sinf(angle);
        camera.view.basis[1][0]=-sinf(angle);camera.view.basis[1][1]=-cosf(angle);camera.view.basis[2][2]=1;
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    struct NativeSceneCameraSnapshot camera;CHECK(NativeSceneSnapshot_CameraPose(scene,0,0.5f,&camera));
    CHECK(fabsf(camera.view.basis[0][0]+1)<0.001f && fabsf(camera.view.basis[0][1])<0.001f && camera.view.basis[1][1]>0.999f);
    /* A change of handedness snaps to the current camera, never through a
     * singular zero-scale matrix. */
    NativeSceneSnapshot_Begin(scene,1,5);
    camera.view=(struct NativeScenePose){.basis={{1,0,0},{0,1,0},{0,0,1}},.position={10,0,0}};
    CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));CHECK(NativeSceneSnapshot_Seal(scene));
    CHECK(NativeSceneSnapshot_CameraPose(scene,0,0.5f,&camera));
    CHECK(camera.view.basis[1][1]==1 && camera.view.position[0]==10);
    /* Authored terrain winding follows either camera handedness. A rejected
     * material update cannot replace the intact triangle's UVs. */
    for(unsigned reflected=0;reflected<2;reflected++) {
        NativeSceneSnapshot_Begin(scene,1,7+reflected);
        camera.view=(struct NativeScenePose){.basis={{1,0,0},{0,1,0},{0,0,1}}};
        if(reflected) camera.view.basis[1][1]=-1;
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneObjectSnapshot object={.id=1,.modelIdentity=9,.supported=1,
            .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={0,0,256}}};
        CHECK(NativeSceneSnapshot_Object(scene,&object));
        struct NativeSceneSourceTriangle source={.object=1,.primitive=1,.local={{-20,-20,0},{20,-20,0},{0,20,0}},
            .cull=1,.reflectedCull=1,.material={.textured=1,.texture={.u={1,2,3}}}};
        CHECK(NativeSceneSnapshot_Triangle(scene,&source));
        struct NativeModelTriangle material=source.material;material.texture.u[0]=99;
        float bad[3][3];memcpy(bad,source.local,sizeof(bad));bad[0][0]+=1;
        CHECK(!NativeSceneSnapshot_Material(scene,1,1,9,bad,&material));
        CHECK(!NativeSceneSnapshot_Material(scene,1,1,10,source.local,&material));
        CHECK(NativeSceneSnapshot_Material(scene,1,1,9,source.local,&material));
        object.id=2;object.supported=0;object.pose.position[0]=1073741824.0f;
        CHECK(NativeSceneSnapshot_Object(scene,&object));
        CHECK(NativeSceneSnapshot_Seal(scene));
        CHECK(Render(scene,1,&out,&stats) && out.count==1 && stats.unsupported==1);
        CHECK(out.triangles[0].source.texture.u[0]==99);
    }
    /* An object teleport must not drag its geometry across the scene. */
    NativeSceneSnapshot_Reset(scene);Capture(scene,10,1,0,0,0);Capture(scene,10,1,2048,0,0);
    CHECK(Render(scene,0,&out,&stats) && out.count==0 && stats.matched==0);
    /* Deletion removes old sources immediately; there are no ghost objects. */
    NativeSceneSnapshot_Begin(scene,1,10);
    camera.view=(struct NativeScenePose){.basis={{1,0,0},{0,1,0},{0,0,1}}};
    CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));CHECK(NativeSceneSnapshot_Seal(scene));
    CHECK(Render(scene,0,&out,&stats) && out.count==0 && stats.objects==0);
    /* A budget overrun rejects the frame, never publishes a truncated scene. */
    NativeSceneSnapshot_Begin(scene,1,11);CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
    struct NativeSceneObjectSnapshot many={.pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    unsigned accepted=0;
    for(unsigned i=0;i<10000;i++) {many.id=i;if(!NativeSceneSnapshot_Object(scene,&many)) break;accepted++;}
    CHECK(accepted>0 && accepted<10000 && !NativeSceneSnapshot_Seal(scene));
    CHECK(!Render(scene,1,&out,&stats) && !out.count);
    /* Invalid input prevents publication of the entire frame. */
    NativeSceneSnapshot_Begin(scene,1,6);camera.h=NAN;CHECK(!NativeSceneSnapshot_Camera(scene,0,&camera));CHECK(!NativeSceneSnapshot_Seal(scene));
    NativeSceneSnapshot_Destroy(scene);
    puts("Scene snapshots: copied sources, pose/history identity, reflected rotation, visibility, clipping and depth regeneration pass.");return 0;
}
