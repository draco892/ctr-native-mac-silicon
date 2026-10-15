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
static int Effects(struct NativeSceneSnapshot *scene)
{
    struct Output out;struct NativeSceneSnapshotStats stats;
    NativeSceneSnapshot_Reset(scene);
    for(unsigned frame=0;frame<2;frame++) {
        NativeSceneSnapshot_Begin(scene,1,20);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        float angle=frame*1.57079632679f,c=cosf(angle),v=sinf(angle);
        struct NativeSceneObjectSnapshot kart={.id=1,.modelIdentity=9,.supported=1,.orderingBias=7,
            .pose={.basis={{c,-v,0},{v,c,0},{0,0,1}},.position={frame*40.0f,0,256}}};
        CHECK(NativeSceneSnapshot_Object(scene,&kart));
        struct NativeSceneEffect wheel={.id=99,.anchor=1,.count=4,.bucket=4,.code=0x2e,.page=96,.clut=4};
        const float local[4][3]={{15,-5,0},{25,-5,0},{15,5,0},{25,5,0}};
        for(unsigned k=0;k<4;k++) {
            wheel.colors[k]=frame ? 0xc8c8c8:0;
            for(unsigned row=0;row<3;row++) {
                wheel.eye[k][row]=kart.pose.position[row];
                for(unsigned axis=0;axis<3;axis++) wheel.eye[k][row]+=kart.pose.basis[row][axis]*local[k][axis];
            }
        }
        CHECK(NativeSceneSnapshot_Effect(scene,&wheel));memset(&wheel,0,sizeof(wheel));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    CHECK(Render(scene,0.5f,&out,&stats) && out.count==2 && out.bucket[0]==11);
    CHECK(fabsf(out.triangles[0].visualXY[0][0]-113.071f)<0.6f);
    CHECK(out.triangles[0].source.colors[0]==0x646464 && out.triangles[0].source.texture.clut==4);
    /* Sky directions have no parallax when the sampled camera translates. */
    for(unsigned frame=0;frame<2;frame++) {
        NativeSceneSnapshot_Begin(scene,1,21);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={frame*400.0f,0,0}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneEffect sky={.id=100,.count=3,.bucket=1023,.code=0x30,
            .flags=NATIVE_EFFECT_DIRECTIONAL|NATIVE_EFFECT_FIXED_BUCKET,.eye={{-20,-20,256},{20,-20,256},{0,20,256}}};
        CHECK(NativeSceneSnapshot_Effect(scene,&sky));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    for(unsigned step=0;step<=120;step++) {
        CHECK(Render(scene,step/120.0f,&out,&stats) && out.count==1 && out.bucket[0]==1023);
        CHECK(fabsf(out.triangles[0].visualXY[0][0]-86)<0.01f);
    }
    /* A billboard keeps camera-facing axes while its centre is sampled in
     * world space; camera rotation must not shear or rotate its quad. */
    for(unsigned frame=0;frame<2;frame++) {
        NativeSceneSnapshot_Begin(scene,1,24);float angle=(frame?10:-10)*0.01745329252f,c=cosf(angle),v=sinf(angle);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{c,0,v},{0,1,0},{-v,0,c}}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneEffect particle={.id=102,.count=4,.bucket=4,.code=0x2e,.flags=NATIVE_EFFECT_BILLBOARD};
        for(unsigned k=0;k<4;k++) {particle.eye[k][0]=v*256+(k&1?10:-10);particle.eye[k][1]=k&2?10:-10;particle.eye[k][2]=c*256;}
        CHECK(NativeSceneSnapshot_Effect(scene,&particle));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    CHECK(Render(scene,0.5f,&out,&stats) && out.count==2);
    CHECK(fabsf(out.triangles[0].visualXY[0][0]-91)<0.6f && fabsf(out.triangles[0].visualXY[1][0]-101)<0.6f);
    CHECK(fabsf(out.triangles[0].visualXY[0][1]-59)<0.6f);
    /* Rain/trail lines crossing the near plane retain a clipped gradient and
     * endpoints; they cannot explode due to a behind-camera division. */
    NativeSceneSnapshot_Begin(scene,1,22);
    struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
        .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
    CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
    struct NativeSceneEffect rain={.id=101,.count=2,.bucket=4,.code=0x52,.page=0xa40,
        .eye={{0,0,-16},{20,10,256}},.colors={0,0xffffff}};
    CHECK(NativeSceneSnapshot_Effect(scene,&rain));CHECK(NativeSceneSnapshot_Seal(scene));
    CHECK(Render(scene,1,&out,&stats) && out.count==1 && out.triangles[0].line && out.triangles[0].depth[0]==32);
    CHECK(out.triangles[0].source.colors[0]>0 && out.triangles[0].source.texture.tpage==0xa40);
    /* Invalid effects leave the frame usable, with their canonical packet. */
    NativeSceneSnapshot_Begin(scene,1,23);CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
    rain.eye[0][0]=NAN;CHECK(!NativeSceneSnapshot_Effect(scene,&rain));CHECK(NativeSceneSnapshot_Seal(scene));
    CHECK(Render(scene,1,&out,&stats) && out.count==0);
    return 0;
}
/* Sprite/depth corner permutations cannot break wheel geometry history. */
static int WheelCorners(struct NativeSceneSnapshot *scene)
{
    struct Output out;struct NativeSceneSnapshotStats stats;
    const float local[4][2]={{15,-5},{25,-5},{15,5},{25,5}};
    for(unsigned a=0;a<8;a++) for(unsigned b=0;b<8;b++) {
        NativeSceneSnapshot_Reset(scene);
        for(unsigned frame=0;frame<2;frame++) {
            NativeSceneSnapshot_Begin(scene,1,25);
            struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
                .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
            CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
            float angle=frame*1.57079632679f,c=cosf(angle),v=sinf(angle);
            struct NativeSceneObjectSnapshot kart={.id=1,.modelIdentity=9,.supported=1,
                .pose={.basis={{c,-v,0},{v,c,0},{0,0,1}},.position={frame*40.0f,0,256}}};
            CHECK(NativeSceneSnapshot_Object(scene,&kart));
            struct NativeSceneEffect wheel={.id=99,.anchor=1,.kind=5,.count=4,.bucket=4,.code=0x2e,.topology=frame?b:a};
            for(unsigned k=0;k<4;k++) {
                unsigned physical=NativeSceneEffect_WheelCorner(wheel.topology,k);
                float x=local[physical][0]+(frame ? (physical&1?4:-4):0);
                float y=local[physical][1]+(frame ? (physical&2?4:-4):0);
                wheel.eye[k][0]=kart.pose.position[0]+c*x-v*y;
                wheel.eye[k][1]=v*x+c*y;wheel.eye[k][2]=256;
                wheel.u[k]=(uint8_t)(physical*10);wheel.v[k]=20;
            }
            CHECK(NativeSceneSnapshot_Effect(scene,&wheel));CHECK(NativeSceneSnapshot_Seal(scene));
        }
        for(unsigned step=0;step<=4;step++) {
            float t=step/4.0f,c=cosf(t*1.57079632679f),v=sinf(t*1.57079632679f);
            CHECK(Render(scene,t,&out,&stats) && out.count==2);
            float expectedX=96+(40*t+c*(15-4*t)-v*(-5-4*t))*0.5f;
            CHECK(fabsf(out.triangles[0].visualXY[0][0]-expectedX)<0.51f);
            CHECK(out.triangles[0].source.texture.u[0]==0 && out.triangles[0].source.texture.u[1]==10);
        }
    }
    /* A retained body must not receive independently interpolated wheels. */
    NativeSceneSnapshot_Begin(scene,1,26);
    struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
        .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
    CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
    struct NativeSceneObjectSnapshot retained={.id=1,.pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    CHECK(NativeSceneSnapshot_Object(scene,&retained));
    struct NativeSceneEffect wheel={.id=99,.anchor=1,.kind=5,.count=4,.bucket=4};
    CHECK(!NativeSceneSnapshot_Effect(scene,&wheel));CHECK(NativeSceneSnapshot_Seal(scene));
    CHECK(Render(scene,0.5f,&out,&stats) && !out.count);
    return 0;
}
/* Material history uses source identity/topology, never screen coordinates. */
static int Materials(struct NativeSceneSnapshot *scene)
{
    struct Output out;struct NativeSceneSnapshotStats stats;
    for(unsigned mode=0;mode<9;mode++) {
        NativeSceneSnapshot_Reset(scene);
        for(unsigned frame=0;frame<2;frame++) {
            NativeSceneSnapshot_Begin(scene,1,30+mode);
            struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
                .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
            CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
            struct NativeSceneObjectSnapshot object={.id=1,.modelIdentity=9,.supported=1,
                .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={0,0,256}}};
            CHECK(NativeSceneSnapshot_Object(scene,&object));
            struct NativeSceneSourceTriangle source={.object=1,.primitive=1,
                .local={{-20,-20,0},{20,-20,0},{0,20,0}},
                .material={.textured=1,.vertices={0,1,2},.texture={.tpage=96,.clut=4}}};
            source.continuousUV=mode==1 || mode==7;
            for(unsigned k=0;k<3;k++) {
                source.material.colors[k]=frame ? 0xc8c8c8:0;
                source.material.texture.u[k]=mode==7 ? (frame?5:250):(frame && (mode==1 || mode==2)?30:10);
                source.material.texture.v[k]=20;
            }
            if(frame && mode==3) source.material.texture.tpage=97;
            if(frame && mode==4) source.material.texture.clut=5;
            if(frame && mode==5) source.material.textureBlend=NATIVE_TEXTURE_BLEND_SEMI;
            if(frame && mode==6) source.material.vertices[0]=3;
            if(mode==8) source.material.textured=0;
            CHECK(NativeSceneSnapshot_Triangle(scene,&source));CHECK(NativeSceneSnapshot_Seal(scene));
        }
        for(unsigned step=0;step<=120;step++) {
            float alpha=step/120.0f;
            CHECK(Render(scene,alpha,&out,&stats) && out.count==1);
            const struct NativeModelTriangle *m=&out.triangles[0].source;
            unsigned expected=(mode==0 || mode==1 || mode==8) ? (unsigned)lroundf(200*alpha):(mode==6 || step==120 ? 200:0);
            CHECK((m->colors[0]&255)==expected && ((m->colors[0]>>8)&255)==expected && ((m->colors[0]>>16)&255)==expected);
            unsigned u=mode==7 ? (step==120?5:250):mode==1 ? (unsigned)lroundf(10+20*alpha):mode==2 && step==120 ? 30:10;
            CHECK(m->texture.u[0]==u);
            CHECK(m->texture.tpage==(mode==3 && step==120 ? 97:96));
            CHECK(m->texture.clut==(mode==4 && step==120 ? 5:4));
        }
        /* Repeated rendering must not consume or mutate material history. */
        CHECK(Render(scene,0.5f,&out,&stats) && out.count==1);
    }
    return 0;
}
static int CameraAspectAndCanonicalPose(struct NativeSceneSnapshot *scene)
{
    NativeSceneSnapshot_Reset(scene);
    struct NativeSceneCameraSnapshot endpoints[2];
    for(unsigned frame=0;frame<2;frame++) {
        float angle=frame*1.0471975512f,c=cosf(angle),v=sinf(angle);
        endpoints[frame]=(struct NativeSceneCameraSnapshot){.view={.basis={{1,0,0},{0,c*0.5625f,-v*0.5625f},{0,v,c}},.position={0,frame*64.0f,0}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        NativeSceneSnapshot_Begin(scene,1,80);CHECK(NativeSceneSnapshot_Camera(scene,0,&endpoints[frame]));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    for(unsigned step=0;step<=120;step++) {
        float t=step/120.0f,c=cosf(t*1.0471975512f),v=sinf(t*1.0471975512f);
        struct NativeSceneCameraSnapshot sampled;CHECK(NativeSceneSnapshot_CameraPose(scene,0,t,&sampled));
        CHECK(fabsf(sampled.view.basis[1][1]-c*0.5625f)<0.0001f);
        CHECK(fabsf(sampled.view.basis[1][2]+v*0.5625f)<0.0001f);
        CHECK(fabsf(sampled.view.basis[2][1]-v)<0.0001f && fabsf(sampled.view.basis[2][2]-c)<0.0001f);
        CHECK(fabsf(sampled.view.position[1]-64*t)<0.0001f);
        if(step==0 || step==120) CHECK(memcmp(&sampled.view,&endpoints[step==120].view,sizeof(sampled.view))==0);
    }
    /* Follow a kart up a ramp while accelerating the camera's pitch/position.
     * Body contact points and independently captured ground patches must stay
     * aligned at every presentation, rather than acquire a false ride height. */
    for(unsigned frame=0;frame<2;frame++) {
        float angle=frame*0.1745329252f,c=cosf(angle),v=sinf(angle);
        NativeSceneSnapshot_Begin(scene,1,82);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,c*0.5625f,-v*0.5625f},{0,v,c}},.position={0,64+frame*64.0f,frame*100.0f}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneObjectSnapshot kart={.id=1,.modelIdentity=9,.supported=1,
            .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={0,frame*64.0f,256+frame*100.0f}}};
        CHECK(NativeSceneSnapshot_Object(scene,&kart));
        struct NativeSceneSourceTriangle body={.object=1,.primitive=1,.local={{-20,0,0},{20,0,0},{0,30,0}},.material={.vertices={0,1,2}}};
        CHECK(NativeSceneSnapshot_Triangle(scene,&body));
        struct NativeSceneEffect patch={.id=2,.kind=7,.count=3,.bucket=4,.code=0x30};
        for(unsigned k=0;k<3;k++) for(unsigned row=0;row<3;row++) for(unsigned axis=0;axis<3;axis++)
            patch.eye[k][row]+=camera.view.basis[row][axis]*(kart.pose.position[axis]+body.local[k][axis]-camera.view.position[axis]);
        CHECK(NativeSceneSnapshot_Effect(scene,&patch));CHECK(NativeSceneSnapshot_Seal(scene));
    }
    for(unsigned step=0;step<=120;step++) {
        float angle=step/120.0f*0.1745329252f,c=cosf(angle),v=sinf(angle);
        struct Output out;struct NativeSceneSnapshotStats stats;
        CHECK(Render(scene,step/120.0f,&out,&stats) && out.count==2);
        float expected=64+(-64*c-256*v)*0.5625f*128/(256*c-64*v);
        CHECK(fabsf(out.triangles[0].visualXY[0][1]-expected)<0.6f);
        CHECK(fabsf(out.triangles[0].visualXY[0][1]-out.triangles[1].visualXY[0][1])<0.6f);
    }
    /* A retained effect uses the current camera: every generated object,
     * vertex and colour must select that same pose, for every requested alpha. */
    for(unsigned frame=0;frame<2;frame++) {
        NativeSceneSnapshot_Begin(scene,1,81);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,0.5625f,0},{0,0,1}},.position={frame*40.0f,0,0}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneObjectSnapshot kart={.id=1,.modelIdentity=9,.supported=1,
            .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={frame*80.0f,0,256}}};
        CHECK(NativeSceneSnapshot_Object(scene,&kart));
        struct NativeSceneSourceTriangle triangle={.object=1,.primitive=1,.local={{-20,-20,0},{20,-20,0},{0,20,0}},
            .material={.vertices={0,1,2},.colors={frame?0xc8c8c8:0,0,0}}};
        CHECK(NativeSceneSnapshot_Triangle(scene,&triangle));
        if(frame) NativeSceneSnapshot_UseCurrentPose(scene);
        CHECK(NativeSceneSnapshot_Seal(scene));
    }
    struct Output reference,out;struct NativeSceneSnapshotStats stats;
    CHECK(Render(scene,1,&reference,&stats) && reference.count==1);
    for(unsigned step=0;step<=120;step++) {
        CHECK(Render(scene,step/120.0f,&out,&stats) && stats.currentPose && out.count==reference.count);
        CHECK(memcmp(out.triangles,reference.triangles,sizeof(out.triangles[0]))==0);
        CHECK(out.triangles[0].source.colors[0]==0xc8c8c8);
    }
    Capture(scene,81,1,0,0,0);CHECK(Render(scene,0.5f,&out,&stats) && !stats.currentPose);
    return 0;
}
static int AnimationPoseContinuity(struct NativeSceneSnapshot *scene)
{
    struct Output out;struct NativeSceneSnapshotStats stats;
    NativeSceneSnapshot_Reset(scene);
    for(unsigned frame=0;frame<3;frame++) {
        NativeSceneSnapshot_Begin(scene,1,83);
        struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
            .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
        CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
        struct NativeSceneObjectSnapshot kart={.id=1,.poseIdentity=frame<2?9:10,.modelIdentity=9^((uint64_t)frame<<48),.animation=frame,.supported=1,
            .pose={.basis={{1,0,0},{0,1,0},{0,0,1}},.position={frame?80:0,0,256}}};
        CHECK(NativeSceneSnapshot_Object(scene,&kart));
        struct NativeSceneSourceTriangle body={.object=1,.primitive=1,.local={{-20,-20,0},{20,-20,0},{0,20,0}},.material={.vertices={0,1,2}}};
        if(frame) body.local[0][0]=-10; /* New animation shape selects current vertices. */
        CHECK(NativeSceneSnapshot_Triangle(scene,&body));
        struct NativeSceneEffect wheel={.id=2,.anchor=1,.kind=5,.count=4,.bucket=4,.code=0x2e};
        const float corners[4][2]={{-5,-5},{5,-5},{-5,5},{5,5}};
        for(unsigned k=0;k<4;k++) {
            unsigned physical=NativeSceneEffect_WheelCorner(0,k);
            wheel.eye[k][0]=kart.pose.position[0]+corners[physical][0];wheel.eye[k][1]=corners[physical][1];wheel.eye[k][2]=256;
        }
        CHECK(NativeSceneSnapshot_Effect(scene,&wheel));CHECK(NativeSceneSnapshot_Seal(scene));
        if(frame==1) {
            CHECK(Render(scene,0.5f,&out,&stats) && out.count==3 && stats.matched==2);
            CHECK(fabsf(out.triangles[0].visualXY[0][0]-111)<0.51f);
            CHECK(fabsf(out.triangles[1].visualXY[0][0]-113.5f)<0.51f);
        }
        if(frame==2) {CHECK(Render(scene,0,&out,&stats) && out.count==3 && !stats.matched);CHECK(fabsf(out.triangles[0].visualXY[0][0]-131)<0.51f);}
    }
    return 0;
}
static int DistantSubpixelGeometry(struct NativeSceneSnapshot *scene)
{
    NativeSceneSnapshot_Begin(scene,1,84);
    struct NativeSceneCameraSnapshot camera={.view={.basis={{1,0,0},{0,1,0},{0,0,1}}},
        .width=192,.height=128,.h=128,.offset={96,64},.nearDepth=32,.farDepth=1024};
    CHECK(NativeSceneSnapshot_Camera(scene,0,&camera));
    struct NativeSceneObjectSnapshot terrain={.id=1,.modelIdentity=9,.supported=1,.pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    CHECK(NativeSceneSnapshot_Object(scene,&terrain));
    struct NativeSceneSourceTriangle sliver={.object=1,.primitive=1,.cull=1,
        .local={{0,0,512},{1,0,512},{0,1,512}},.material={.vertices={0,1,2}}};
    CHECK(NativeSceneSnapshot_Triangle(scene,&sliver));
    sliver.primitive=2;sliver.cull=-1;CHECK(NativeSceneSnapshot_Triangle(scene,&sliver));
    CHECK(NativeSceneSnapshot_Seal(scene));
    struct Output out;struct NativeSceneSnapshotStats stats;
    CHECK(Render(scene,1,&out,&stats) && out.count==1 && stats.culled==1);
    CHECK(out.triangles[0].signedArea==0);
    CHECK(out.triangles[0].visualXY[1][0]>out.triangles[0].visualXY[0][0]);
    CHECK(out.triangles[0].visualXY[2][1]>out.triangles[0].visualXY[0][1]);
    return 0;
}
int main(void)
{
    struct NativeSceneSnapshot *scene=NativeSceneSnapshot_Create();CHECK(scene);
    struct Output out;struct NativeSceneSnapshotStats stats;
    CHECK(DistantSubpixelGeometry(scene)==0);
    CHECK(AnimationPoseContinuity(scene)==0);
    CHECK(CameraAspectAndCanonicalPose(scene)==0);
    CHECK(Effects(scene)==0);
    CHECK(WheelCorners(scene)==0);
    CHECK(Materials(scene)==0);
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
