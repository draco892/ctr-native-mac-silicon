#include <platform/native_terrain_material.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s line %d\n",#c,__LINE__); return 1; } } while(0)
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
int main(void)
{
    u8 storage[0x500+1]={0},ptr[28]={0}; u8 *asset=storage+1;
    for(unsigned k=0;k<9;k++) { Put16(asset+0x100+2*k,(u16)k); Put16(asset+0x15c+16*k,(u16)(k*100)); CTR_WriteU32LE(asset+0x164+16*k,0x00808080); }
    const u32 slots[]={0x11c,0x120,0x124,0x128,0x34c,0x350};
    CTR_WriteU32LE(ptr,24);
    for(unsigned k=0;k<6;k++) { CTR_WriteU32LE(ptr+4+4*k,slots[k]); CTR_WriteU32LE(asset+slots[k],k<4 ? 0x300 : k==4 ? 0x300 : 0x400); }
    for(unsigned g=0;g<2;g++) for(unsigned lod=0;lod<3;lod++) {
        u8 *tex=asset+(g ? 0x400 : 0x300)+12*lod;
        tex[0]=(u8)(10+g*40+lod); tex[1]=20; tex[4]=30;tex[5]=20;tex[8]=10;tex[9]=40;tex[10]=30;tex[11]=40;
        Put16(tex+2,0x100); Put16(tex+6,0x80);
    }
    Put16(asset+0x344,2); Put16(asset+0x346,0xffff); Put16(asset+0x348,1);
    struct NativePtrMapEntry entries[6]; struct NativePtrMapView map;
    CHECK(NativePtrMap_Decode(asset,0x500,ptr,sizeof(ptr),entries,6,&map)==NATIVE_PTRMAP_OK);
    struct NativeLevelView level={.map=&map};
    struct NativeMeshView mesh={.quadCount=1,.vertexCount=9,.quads=asset+0x100,.vertices=asset+0x15c};
    struct NativeTerrainTriangle out;
    for(unsigned mode=0;mode<24;mode++) {
        CTR_WriteU32LE(asset+0x114,mode<<8);
        unsigned count=0;
        for(unsigned t=0;t<2;t++) {
            enum NativeAssetResult status=NativeTerrain_GetTriangle(&level,&mesh,0,0,t,2,0,&out);
            CHECK(status==NATIVE_ASSET_OK || status==NATIVE_ASSET_NOT_FOUND);
            if(status==NATIVE_ASSET_OK) { count++; CHECK(out.textured && out.texture.tpage==0x80 && out.texture.clut==0x100); }
        }
        CHECK(count==(mode<8 ? 2u : 1u));
    }
    CTR_WriteU32LE(asset+0x114,0);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,2,0,&out)==NATIVE_ASSET_OK);
    const u16 primary[3]={0,4,5}; CHECK(memcmp(primary,out.geometry.indices,sizeof(primary))==0);
    CHECK(out.texture.u[0]==12 && out.texture.v[2]==40);
    CHECK(NativeTerrain_FrontFacing(&out,10) && !NativeTerrain_FrontFacing(&out,-10));
    CTR_WriteU32LE(asset+0x114,1<<8);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_OK);
    const u16 rotated[3]={6,4,5}; CHECK(memcmp(rotated,out.geometry.indices,sizeof(rotated))==0);
    CHECK(out.texture.u[0]==30 && out.texture.u[1]==10 && out.texture.u[2]==30);
    CHECK(NativeTerrain_FrontFacing(&out,-10) && !NativeTerrain_FrontFacing(&out,10));
    CTR_WriteU32LE(asset+0x114,0x80000000);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_OK);
    CHECK(NativeTerrain_FrontFacing(&out,-10) && !NativeTerrain_FrontFacing(&out,0));
    // Tagged animated texture selects immutable frame pointers at each tick.
    CTR_WriteU32LE(asset+0x11c,0x341);
    CHECK(NativePtrMap_Decode(asset,0x500,ptr,sizeof(ptr),entries,6,&map)==NATIVE_PTRMAP_OK);
    u8 snapshot[0x500]; memcpy(snapshot,asset,sizeof(snapshot));
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_OK && out.texture.u[0]==50);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,1,&out)==NATIVE_ASSET_OK && out.texture.u[0]==10);
    CHECK(memcmp(snapshot,asset,sizeof(snapshot))==0);
    Put16(asset+0x344,0);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_INVALID_DATA && !out.textured);
    Put16(asset+0x344,2); Put16(asset+0x348,32);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_INVALID_DATA);
    Put16(asset+0x348,1); CTR_WriteU32LE(asset+0x350,0x4ff);
    CHECK(NativePtrMap_Decode(asset,0x500,ptr,sizeof(ptr),entries,6,&map)==NATIVE_PTRMAP_OK);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_INVALID_DATA);
    CTR_WriteU32LE(asset+0x114,24<<8);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_INVALID_DATA);
    CHECK(NativeTerrain_GetTriangle(&level,&mesh,0,4,0,0,0,&out)==NATIVE_ASSET_INDEX_OUT_OF_RANGE);
    CHECK(NativeTerrain_GetTriangle(NULL,&mesh,0,0,0,0,0,&out)==NATIVE_ASSET_INVALID_ARGUMENT);
    puts("Terrain material OK"); return 0;
}
