#include <common.h>
#include <platform/native_scene_assets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game/RenderLevel/NativeSceneSnapshot.c"
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
struct sData sdata_static;
struct NativeSceneAssets gNativeSceneAssets;
/* Fixtures supply copied geometry directly, without retail assets. */
enum NativeAssetResult NativeSceneAssets_GetModel(const struct NativeSceneAssets *a,const void *p,struct NativeModelView *v)
{(void)a;(void)p;(void)v;return NATIVE_ASSET_NOT_FOUND;}
enum NativeAssetResult NativeSceneAssets_GetLevel(const struct NativeSceneAssets *a,const void *p,struct NativeLevelView *v)
{(void)a;(void)p;(void)v;return NATIVE_ASSET_NOT_FOUND;}
static struct GameTracker tracker;
static struct DB backBuffer;
static u32 ot[6+4*1024],packets[6][7],environments[4][4];
static int identities[5];
static void Link(u32 *slot,u32 *packet,unsigned words)
{packet[0]=(words<<24)|(*slot&0xffffffu);*slot=NativeGpuLinks_FromHostPointer(packet);}
static void Capture(unsigned views,unsigned mode,int offscreen)
{
    NativeVisual_Reset();NativeGpuLinks_Reset();
    CHECK(NativeGpuLinks_RegisterRange(ot,sizeof(ot),NULL));
    CHECK(NativeGpuLinks_RegisterRange(packets,sizeof(packets),NULL));
    CHECK(NativeGpuLinks_RegisterRange(environments,sizeof(environments),NULL));
    unsigned count=6+views*1024;
    ot[0]=NATIVE_GPU_LINK_TERMINATOR;
    for(unsigned i=1;i<count;i++) ot[i]=NativeGpuLinks_FromHostPointer(&ot[i-1]);
    memset(&tracker,0,sizeof(tracker));memset(packets,0,sizeof(packets));
    tracker.backBuffer=&backBuffer;tracker.numPlyrCurrGame=(char)views;tracker.gameMode1=mode;tracker.pushBuffer_UI.ptrOT=ot+1;
    for(unsigned v=0;v<views;v++) {
        struct PushBuffer *pb=&tracker.pushBuffer[v];pb->ptrOT=ot+6+(views-1-v)*1024;
        pb->rect.x=(short)v;pb->rect.w=192;pb->rect.h=128;pb->distanceToScreen_PREV=128;
        for(unsigned axis=0;axis<3;axis++) pb->matrix_ViewProj.m[axis][axis]=4096;
    }
    NativeVisual_BeginCapture(1,mode+views);NativeSceneGameSnapshot_Begin(&tracker,1,mode+views);
    CHECK(sceneCapture);
    for(unsigned v=0;v<views;v++) {
        u32 *p=packets[v];p[1]=0x30000000|v;p[2]=0x11110001;p[4]=0x22220002;p[6]=0x33330003;
        NativeVisual_Push(1,&identities[v],42^v,1);NativeVisual_ModelViewport(v);
        NativeVisual_RecordPacket(p,6);NativeVisual_Pop();
        struct NativeSceneObjectSnapshot object={.id=NativeVisual_ObjectID(&identities[v]),.modelIdentity=42,.supported=1,
            .viewportMask=NATIVE_SCENE_WORLD_MEMBERSHIP,.pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
        CHECK(NativeSceneSnapshot_Object(sceneGame,&object));
        struct NativeSceneSourceTriangle source={.object=object.id,.primitive=1,
            .material={.colors={0x112233,0x112233,0x112233}},.local={{-20,-20,256},{20,-20,256},{0,20,256}}};
        CHECK(NativeSceneSnapshot_Triangle(sceneGame,&source));
        Link(&tracker.pushBuffer[v].ptrOT[4],p,6);
        /* Viewport draw environment at the beginning of each world chain. */
        u32 *env=environments[v];
        env[1]=0xe3000000|v;env[2]=0xe4000000|(v+191)|(127u<<10);env[3]=0xe5000000|v;
        Link(&tracker.pushBuffer[v].ptrOT[1023],env,3);
    }
    /* A model in the UI must survive once, without generating a world copy. */
    u32 *ui=packets[4];ui[1]=0x30010203;ui[2]=0x44440004;ui[4]=0x55550005;ui[6]=0x66660006;
    NativeVisual_Push(1,&identities[4],42,1);NativeVisual_RecordPacket(ui,6);NativeVisual_Pop();
    struct NativeSceneObjectSnapshot uiObject={.id=NativeVisual_ObjectID(&identities[4]),.modelIdentity=42,.supported=1,
        .viewportMask=NATIVE_SCENE_WORLD_MEMBERSHIP,.pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    CHECK(NativeSceneSnapshot_Object(sceneGame,&uiObject));
    struct NativeSceneSourceTriangle uiSource={.object=uiObject.id,.primitive=1,.local={{-20,-20,256},{20,-20,256},{0,20,256}}};
    CHECK(NativeSceneSnapshot_Triangle(sceneGame,&uiSource));Link(&tracker.pushBuffer_UI.ptrOT[2],ui,6);
    if(offscreen) {packets[5][1]=0xe30000ff;Link(&tracker.pushBuffer[0].ptrOT[offscreen],packets[5],1);}
    NativeVisual_EndCapture();NativeSceneGameSnapshot_Seal();
}
int main(void)
{
    unsigned modes[]={MAIN_MENU,GAME_CUTSCENE,ADVENTURE_ARENA,0};
    for(unsigned mode=0;mode<4;mode++) for(unsigned views=1;views<=4;views++) {
        Capture(views,modes[mode],0);
        u32 original[6][7];memcpy(original,packets,sizeof(original));
        for(unsigned sample=0;sample<=4;sample++) {
            NativeVisual_BeginScenePresentation(sample/4.0f);
            const u32 *p=NativeSceneGameSnapshot_Presentation(&tracker,sample/4.0f);
            CHECK(p!=&tracker.pushBuffer[0].ptrOT[1023]);
            unsigned setup=0,triangles=0,ui=0,walk=0;
            for(;;) {
                CHECK(++walk<1000);unsigned words=p[0]>>24;
                if(words && (p[1]>>24)==0xe3) {CHECK((p[1]&255)==setup);setup++;}
                if(words && (p[1]>>24)==0x30) {
                    if(p[1]==0x30010203) {CHECK(setup==views);CHECK(memcmp(p+1,packets[4]+1,24)==0);ui++;}
                    else {CHECK((p[1]&0xffffff)==0x112233);CHECK(setup>=1 && setup<=views);triangles++;}
                }
                if(NativeGpuLinks_IsTerminator(p[0])) break;
                p=NativeGpuLinks_ToHostPointer(p[0]&0xffffffu);CHECK(p);
            }
            CHECK(setup==views && triangles==views && ui==1);
            CHECK(memcmp(original,packets,sizeof(original))==0);NativeVisual_EndPresentation();
        }
    }
    for(unsigned nested=0;nested<2;nested++) {
        Capture(2,0,nested?1023:5);
        CHECK(sceneCanonicalReasons&NATIVE_SCENE_CANONICAL_OFFSCREEN);
        for(unsigned i=0;i<4;i++) CHECK(NativeSceneGameSnapshot_Presentation(&tracker,i/3.0f)==&tracker.pushBuffer[0].ptrOT[1023]);
    }
    /* The crate intro must not alternate generated fragments and canonical
     * split geometry, even with interpolation requested and valid history. */
    Capture(1,MAIN_MENU,0);
    u32 introPackets[6][7];memcpy(introPackets,packets,sizeof(introPackets));
    tracker.levelID=NAUGHTY_DOG_CRATE;
    for(unsigned mode=0;mode<4;mode++) {
        tracker.gameMode1=modes[mode];
        NativeSceneGameSnapshot_Begin(&tracker,1,100+mode);
        CHECK(!sceneCapture);NativeSceneGameSnapshot_Seal();
        for(unsigned step=0;step<=120;step++) {
            CHECK(NativeSceneGameSnapshot_Presentation(&tracker,step/120.0f)==&tracker.pushBuffer[0].ptrOT[1023]);
            struct NativeSceneGameSnapshotStats stats=NativeSceneGameSnapshot_GetStats();
            CHECK(stats.fallback && !stats.regenerated && stats.canonicalPoseReasons==NATIVE_SCENE_CANONICAL_SCRIPTED_INTRO);
            CHECK(memcmp(introPackets,packets,sizeof(introPackets))==0);
        }
    }
    tracker.levelID=MAIN_MENU_LEVEL;tracker.gameMode1=MAIN_MENU;
    NativeSceneGameSnapshot_Begin(&tracker,1,200);CHECK(sceneCapture);
    NativeSceneSnapshot_Destroy(sceneGame);sceneGame=NULL;
    puts("Scene game layouts: 1-4 view state/order, UI membership, mode gates, immutable packets and offscreen fallback pass.");
    return 0;
}
