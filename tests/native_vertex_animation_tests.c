#include <platform/native_vertex_animation.h>
#include <ctr_math.h>
#include <psx/inline_c.h>
#include <psx/gtereg.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at %d\n",#c,__LINE__); return 1; } } while(0)
#define COMMON_H
struct LevVertex { SVec3 pos; u16 flags; u8 color_hi[4],color_lo[4]; };
struct SCVert { struct LevVertex *v; int offset_pos_xy,offset_pos_zw,offset_color_rgba; };
struct OVert { s16 data[28]; };
struct WaterVert { struct LevVertex *v; struct OVert *w; };
struct TextureLayout { u8 bytes[12]; };
static struct { u32 trigApprox[1024]; } data;
static const s16 trigs[1024][2]={
#include "../platform/native_trig_table.inc"
};
extern int GTE_operator(int op);
void MTC2(unsigned int value,int reg) { gteRegs.CP2D.p[reg].d=value; }
void CTC2(unsigned int value,int reg) { gteRegs.CP2C.p[reg].d=value; }
unsigned int MFC2(int reg) { return gteRegs.CP2D.p[reg].d; }
int doCOP2(int op) { return GTE_operator(op); }
#include "../game/RenderLevel/AnimateWater.c"
static const u32 slots[]={0,0x210,0x28,0x38,0x44,0x300,0x304,0x170,0x178,0x3a0};
static void Fixture(u8 asset[1024],u8 ptr[4+sizeof(slots)])
{
    memset(asset,0,1024); CTR_WriteU32LE(asset,0x200); CTR_WriteU32LE(asset+0x204,1); CTR_WriteU32LE(asset+0x210,0x240);
    CTR_WriteU32LE(asset+0x34,1); CTR_WriteU32LE(asset+0x38,0x300); CTR_WriteU32LE(asset+0x44,0x380); CTR_WriteU32LE(asset+0x28,0x390);
    CTR_WriteU32LE(asset+0x300,0x240); CTR_WriteU32LE(asset+0x304,0x340); CTR_WriteU32LE(asset+0x390,1); CTR_WriteU16LE(asset+0x380,0x91);
    CTR_WriteU32LE(asset+0x170,0x390); CTR_WriteU32LE(asset+0x174,1); CTR_WriteU32LE(asset+0x178,0x3a0); CTR_WriteU32LE(asset+0x3a0,0x240);
    CTR_WriteU32LE(asset+0x3a4,(u16)-500|((u32)(u16)1300<<16)); CTR_WriteU32LE(asset+0x3a8,(u16)-1700|((u32)0xc4d2<<16)); CTR_WriteU32LE(asset+0x3ac,0x00f08040);
    for(unsigned i=0;i<16;i++) asset[0x240+i]=(u8)(i*13);
    u32 random=0x12345678;
    for(unsigned i=0;i<28;i++) { random=random*1664525u+1013904223u; CTR_WriteU16LE(asset+0x340+i*2,(u16)random); }
    CTR_WriteU32LE(ptr,sizeof(slots)); for(unsigned k=0;k<sizeof(slots)/sizeof(*slots);k++) CTR_WriteU32LE(ptr+4+k*4,slots[k]);
}
int main(void)
{
    for(unsigned i=0;i<1024;i++) data.trigApprox[i]=(u16)trigs[i][0]|((u32)(u16)trigs[i][1]<<16);
    u8 asset[1024],ptr[4+sizeof(slots)],output[16],saved[1024]; Fixture(asset,ptr); memcpy(saved,asset,sizeof(asset));
    struct NativePtrMapEntry entries[sizeof(slots)/sizeof(*slots)]; struct NativePtrMapView map;
    CHECK(NativePtrMap_Decode(asset,sizeof(asset),ptr,sizeof(ptr),entries,sizeof(entries)/sizeof(*entries),&map)==NATIVE_PTRMAP_OK);
    struct NativeLevelView level; struct NativeMeshView mesh,animated;
    CHECK(NativeLevel_Open(&map,&level)==NATIVE_ASSET_OK && NativeLevel_GetMesh(&level,&mesh)==NATIVE_ASSET_OK);
    struct NativeVertexAnimationView a;
    CHECK(NativeVertexAnimation_Open(&level,&mesh,&a)==NATIVE_ASSET_OK && a.count==1 && !a.scenery);
    struct TextureLayout env; memcpy(&env,asset+0x380,sizeof(env));
    struct OVert samples; memcpy(&samples,asset+0x340,sizeof(samples));
    for(u32 tick=0;tick<224;tick++) {
        struct LevVertex reference; memcpy(&reference,asset+0x240,sizeof(reference));
        struct WaterVert record={&reference,&samples}; int visibility=1;
        memset(&gteRegs,0,sizeof(gteRegs)); GTERegisters before=gteRegs;
        CHECK(NativeVertexAnimation_Apply(&a,tick,NULL,NULL,0,output,sizeof(output),&animated)==NATIVE_ASSET_OK && animated.vertices==output);
        CHECK(memcmp(&gteRegs,&before,sizeof(before))==0 && memcmp(asset,saved,sizeof(asset))==0);
        AnimateWater1P((int)tick,1,&record,&env,&visibility);
        CHECK(memcmp(output,&reference,sizeof(reference))==0);
    }
    u8 hidden[4]={0},visible[4]={1}; const void *masks[4]={hidden,hidden,hidden,visible}; size_t sizes[4]={4,4,4,4};
    CHECK(NativeVertexAnimation_Apply(&a,7,masks,sizes,3,output,sizeof(output),&animated)==NATIVE_ASSET_OK && memcmp(output,asset+0x240,16)==0);
    CHECK(NativeVertexAnimation_Apply(&a,7,masks,sizes,4,output,sizeof(output),&animated)==NATIVE_ASSET_OK && memcmp(output,asset+0x240,16)!=0);
    sizes[3]=3; CHECK(NativeVertexAnimation_Apply(&a,0,masks,sizes,4,output,sizeof(output),&animated)==NATIVE_ASSET_INVALID_DATA && animated.vertices==NULL);
    CHECK(NativeVertexAnimation_Apply(&a,0,NULL,NULL,0,output,15,&animated)==NATIVE_ASSET_OUTPUT_TOO_SMALL);
    CHECK(NativeVertexAnimation_Apply(&a,0,NULL,NULL,0,asset+0x240,16,&animated)==NATIVE_ASSET_INVALID_ARGUMENT);
    CTR_WriteU32LE(asset+0xdc,4); memcpy(saved,asset,sizeof(asset));
    CHECK(NativeVertexAnimation_Open(&level,&mesh,&a)==NATIVE_ASSET_OK && a.scenery);
    for(u32 tick=0;tick<4096;tick++) {
        struct LevVertex reference; memcpy(&reference,asset+0x240,sizeof(reference));
        struct SCVert record={&reference,(int)CTR_ReadU32LE(asset+0x3a4),(int)CTR_ReadU32LE(asset+0x3a8),(int)CTR_ReadU32LE(asset+0x3ac)}; int visibility=1;
        memset(&gteRegs,0,sizeof(gteRegs)); GTERegisters before=gteRegs;
        CHECK(NativeVertexAnimation_Apply(&a,tick,NULL,NULL,0,output,sizeof(output),&animated)==NATIVE_ASSET_OK);
        CHECK(memcmp(&gteRegs,&before,sizeof(before))==0 && memcmp(asset,saved,sizeof(asset))==0);
        AnimateQuad((int)(tick<<7),1,&record,&visibility);
        CHECK(memcmp(output,&reference,sizeof(reference))==0);
    }
    CTR_WriteU32LE(asset+0xdc,0); CTR_WriteU32LE(asset+0x304,1000);
    CHECK(NativePtrMap_Decode(asset,sizeof(asset),ptr,sizeof(ptr),entries,sizeof(entries)/sizeof(*entries),&map)==NATIVE_PTRMAP_OK);
    CHECK(NativeVertexAnimation_Open(&level,&mesh,&a)==NATIVE_ASSET_INVALID_DATA && a.map==NULL);
    Fixture(asset,ptr); CTR_WriteU32LE(asset+0x300,0x242);
    CHECK(NativePtrMap_Decode(asset,sizeof(asset),ptr,sizeof(ptr),entries,sizeof(entries)/sizeof(*entries),&map)==NATIVE_PTRMAP_OK);
    CHECK(NativeVertexAnimation_Open(&level,&mesh,&a)==NATIVE_ASSET_INVALID_DATA);
    puts("Native water/scenery animation matches production GTE paths: 224 water ticks, 4096 scenery ticks, mask union and immutable assets OK"); return 0;
}
