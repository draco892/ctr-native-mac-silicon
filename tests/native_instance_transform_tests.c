#include <platform/native_instance_transform.h>
#include <psx/gtereg.h>
#include <stdio.h>
#include <string.h>
extern int GTE_operator(int op);
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s line %d\n",#c,__LINE__); return 1; } } while(0)
static const struct NativeModelMatrix identity={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
// Exercise the resident GTE matrix-column primitive, independently from Compose.
static struct NativeModelMatrix ReferenceMul(const struct NativeModelMatrix *a,const struct NativeModelMatrix *b)
{
    struct NativeModelMatrix out={0}; memset(&gteRegs,0,sizeof(gteRegs));
    for(unsigned i=0;i<9;i++) {
        if(i%2) gteRegs.CP2C.p[i/2].sw.h=a->m[i/3][i%3]; else gteRegs.CP2C.p[i/2].sw.l=a->m[i/3][i%3];
    }
    for(unsigned c=0;c<3;c++) {
        gteRegs.CP2D.p[0].sw.l=b->m[0][c]; gteRegs.CP2D.p[0].sw.h=b->m[1][c]; gteRegs.CP2D.p[1].sw.l=b->m[2][c];
        GTE_operator(0x0486012);
        for(unsigned r=0;r<3;r++) out.m[r][c]=(s16)gteRegs.CP2D.p[9+r].sd;
    }
    return out;
}
static int TestRotations(void)
{
    struct NativeModelMatrix out; const s16 zero[3]={0};
    CHECK(NativeInstance_Rotation(zero,&out)==NATIVE_ASSET_OK && memcmp(&out,&identity,sizeof(out))==0);
    const s16 quarter[3]={0,1024,0};
    CHECK(NativeInstance_Rotation(quarter,&out)==NATIVE_ASSET_OK && out.m[0][2]==4096 && out.m[2][0]==-4096);
    const s16 wrapped[3]={0,-3072,0}; struct NativeModelMatrix wrap;
    CHECK(NativeInstance_Rotation(wrapped,&wrap)==NATIVE_ASSET_OK && memcmp(&wrap,&out,sizeof(out))==0);
    const s16 mixed[3]={1024,1024,1024};
    const struct NativeModelMatrix expectedMixed={.m={{4096,0,0},{0,0,-4096},{0,4096,0}}};
    CHECK(NativeInstance_Rotation(mixed,&out)==NATIVE_ASSET_OK && memcmp(&out,&expectedMixed,sizeof(out))==0);
    u32 random=0x12345678;
    for(unsigned trial=0;trial<10000;trial++) {
        s16 angles[3]; struct NativeModelMatrix axes[3],expected;
        for(unsigned i=0;i<3;i++) {
            random=random*1664525u+1013904223u; angles[i]=(s16)((int)(random%65536)-32768);
            s16 single[3]={0}; single[i]=angles[i];
            CHECK(NativeInstance_Rotation(single,&axes[i])==NATIVE_ASSET_OK);
        }
        expected=ReferenceMul(&axes[1],&axes[0]); expected=ReferenceMul(&expected,&axes[2]);
        CHECK(NativeInstance_Rotation(angles,&out)==NATIVE_ASSET_OK && memcmp(&out,&expected,sizeof(out))==0);
    }
    CHECK(NativeInstance_Rotation(NULL,&out)==NATIVE_ASSET_INVALID_ARGUMENT && out.m[0][0]==0);
    return 0;
}
static int TestInstance(void)
{
    u8 asset[128]={0},ptr[8]={0}; struct NativePtrMapEntry entry; struct NativePtrMapView map;
    asset[18]=1; CTR_WriteU32LE(asset+20,24);
    for(unsigned i=0;i<3;i++) { asset[48+i*2]=0; asset[49+i*2]=16; }
    CTR_WriteU32LE(ptr,4); CTR_WriteU32LE(ptr+4,20);
    CHECK(NativePtrMap_Decode(asset,sizeof(asset),ptr,sizeof(ptr),&entry,1,&map)==NATIVE_PTRMAP_OK);
    struct NativeInstanceDefView instance={.scale={4096,8192,2048},.position={10,20,100}};
    CHECK(NativeModel_Open(&map,0,&instance.model)==NATIVE_ASSET_OK);
    struct NativeInstanceCamera camera={.view=identity,.position={1,2,0},.h=256,.offset={123,456},.dqa=-7,.dqb=8};
    struct NativeProjectionConfig out; s32 depth;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK);
    CHECK(depth==100 && out.translation[0]==36 && out.translation[1]==72 && out.translation[2]==400);
    CHECK(out.rotation.m[0][0]==4096 && out.rotation.m[1][1]==8192 && out.rotation.m[2][2]==2048);
    CHECK(out.h==256 && out.offset[0]==123 && out.dqa==-7 && out.dqb==8);
    instance.rotation[1]=1024;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK && out.rotation.m[0][2]==2048 && out.rotation.m[2][0]==-4096);
    instance.flags=0x8000000;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK && out.translation[2]==100);
    instance.flags=0x400;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK && out.translation[0]==40 && out.translation[1]==80);
    instance.flags=0; instance.rotation[1]=0; instance.position[2]=4096;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK && out.rotation.m[0][0]==1024 && out.translation[2]==4096);
    instance.flags=0x200;
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK && out.rotation.m[0][0]==1536);
    // Preserve the legacy order: scale model rotation before the view multiply.
    const s16 viewAngles[3]={311,-777,911}; NativeInstance_Rotation(viewAngles,&camera.view);
    instance.rotation[0]=223; instance.rotation[1]=667; instance.rotation[2]=51;
    instance.scale[0]=3333; instance.scale[1]=5777; instance.scale[2]=1223; instance.flags=0;
    struct NativeModelMatrix world,scaled,expected;
    NativeInstance_Rotation(instance.rotation,&world);
    CHECK(NativeInstance_Projection(&instance,0,&camera,&out,&depth)==NATIVE_ASSET_OK);
    const s16 modelScale[3]={4096,4096,4096};
    NativeModelMatrix_Build(&world,modelScale,instance.scale,depth,0,&scaled);
    expected=ReferenceMul(&camera.view,&scaled);
    CHECK(memcmp(&out.rotation,&expected,sizeof(expected))==0);
    CHECK(NativeInstance_Projection(&instance,1,&camera,&out,&depth)!=NATIVE_ASSET_OK && depth==0 && out.h==0);
    CHECK(NativeInstance_Projection(NULL,0,&camera,&out,&depth)==NATIVE_ASSET_INVALID_ARGUMENT && depth==0);
    return 0;
}
int main(void) { if(TestRotations() || TestInstance()) return 1; puts("Instance transforms: authored rotation/scale/position, flags and 10000 GTE composition comparisons passed."); return 0; }
