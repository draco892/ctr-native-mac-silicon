#include <platform/native_raster.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while(0)
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static struct NativeDrawTriangle Triangle(void)
{
	struct NativeDrawTriangle t={.screen={{0,0},{4,0},{0,4}},.depth={100,100,100}};
	for(unsigned i=0;i<3;i++) t.source.colors[i]=0x0000ff;
	return t;
}
static int TestCoverageAndDepth(void)
{
	u8 storage[50],*rgb=storage+1,black[3]={0}; u32 depth[16]; struct NativeRasterView target; struct NativeRasterStats stats;
	memset(storage,0xa5,sizeof(storage));
	CHECK(NativeRaster_Bind(rgb,48,depth,16,4,4,&target)==NATIVE_ASSET_OK);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK);
	struct NativeDrawTriangle t=Triangle();
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.written==6 && stats.covered==6);
	CHECK(rgb[0]==255 && rgb[1]==0 && rgb[2]==0 && depth[0]==100 && depth[15]==UINT32_MAX);
	u8 first[48]; memcpy(first,rgb,48);
	// Reverse winding and corresponding attributes: identical coverage.
	struct NativeDrawTriangle reverse=t;
	memcpy(reverse.screen[1],t.screen[2],sizeof(t.screen[2])); memcpy(reverse.screen[2],t.screen[1],sizeof(t.screen[1]));
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK);
	CHECK(NativeRaster_Draw(&target,&reverse,NULL,&stats)==NATIVE_ASSET_OK && memcmp(rgb,first,48)==0);
	// Adjacent triangle shares diagonal without holes or double coverage.
	t=(struct NativeDrawTriangle){.screen={{4,0},{4,4},{0,4}},.depth={100,100,100}};
	for(unsigned i=0;i<3;i++) t.source.colors[i]=0x00ff00;
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.written==10 && stats.covered==10);
	for(unsigned i=0;i<16;i++) CHECK(depth[i]==100);
	CHECK(storage[0]==0xa5 && storage[49]==0xa5);
	// Equal/far depth loses, near depth wins independently of draw order.
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.written==0);
	for(unsigned i=0;i<3;i++) { t.depth[i]=101; t.source.colors[i]=0xff0000; }
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.written==0);
	for(unsigned i=0;i<3;i++) t.depth[i]=99;
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.written==10 && rgb[47]==255);
	// Affine RGB/depth at pixel (0,0) has weights (3/4,1/8,1/8).
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK); t=Triangle();
	t.source.colors[0]=0; t.source.colors[1]=0x0000ff; t.source.colors[2]=0x00ff00;
	t.depth[0]=100; t.depth[1]=108; t.depth[2]=116;
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && rgb[0]==31 && rgb[1]==31 && depth[0]==103);
	// Zero depth and divide overflow reject; offscreen scissoring is bounded.
	t.depth[0]=0; CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.skipped);
	t=Triangle(); t.projectionFlags=0x20000; CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK && stats.skipped);
	t=Triangle(); for(unsigned i=0;i<3;i++) { t.screen[i][0]-=2; t.screen[i][1]-=2; }
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_OK);
	CHECK(NativeRaster_Bind(rgb,47,depth,16,4,4,&target)==NATIVE_ASSET_INVALID_ARGUMENT && target.rgb==NULL);
	return 0;
}
static int TestTextureAndAtomicity(void)
{
	u8 rgb[48],black[3]={0},copy[48]; u32 depth[16],depthCopy[16];
	struct NativeRasterView target; struct NativeRasterStats stats;
	u8 *pixels=calloc(1,NATIVE_VRAM_BYTES); CHECK(pixels!=NULL); struct NativeVramView vram;
	CHECK(NativeVram_Bind(pixels,NATIVE_VRAM_BYTES,&vram)==NATIVE_ASSET_OK);
	CHECK(NativeRaster_Bind(rgb,48,depth,16,4,4,&target)==NATIVE_ASSET_OK);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK);
	struct NativeDrawTriangle t=Triangle(); t.source.textured=1; t.source.texture.tpage=256;
	for(unsigned i=0;i<3;i++) t.source.colors[i]=0x808080;
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.transparent==6 && stats.written==0 && depth[0]==UINT32_MAX);
	Put16(pixels,0x8000);
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==6 && stats.blended==6 && depth[0]==UINT32_MAX && rgb[0]==0);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK); Put16(pixels,31);
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==6 && rgb[0]==255 && rgb[1]==0);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK);
	Put16(pixels+2,0x03e0); t.source.texture.u[1]=4;
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && rgb[0]==255 && rgb[3]==0 && rgb[4]==255);
	// A valid early fragment followed by an invalid palette address writes nothing.
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK); memset(pixels,0,NATIVE_VRAM_BYTES);
	t.source.texture.tpage=128; t.source.texture.clut=63;
	Put16(pixels+2,255); Put16(pixels+1008*2,0x801f);
	memcpy(copy,rgb,48); memcpy(depthCopy,depth,sizeof(depth));
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_INVALID_DATA && stats.written==0);
	CHECK(memcmp(copy,rgb,48)==0 && memcmp(depthCopy,depth,sizeof(depth))==0);
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_INVALID_ARGUMENT);
	free(pixels); return 0;
}
static int TestMaterialBlending(void)
{
    u8 rgb[48],background[3]={101,77,55}; u32 depth[16];
    struct NativeRasterView target; struct NativeRasterStats stats;
    u8 *pixels=calloc(1,NATIVE_VRAM_BYTES); CHECK(pixels);
    struct NativeVramView vram;
    CHECK(NativeVram_Bind(pixels,NATIVE_VRAM_BYTES,&vram)==NATIVE_ASSET_OK);
    CHECK(NativeRaster_Bind(rgb,sizeof(rgb),depth,16,4,4,&target)==NATIVE_ASSET_OK);
    struct NativeDrawTriangle t=Triangle(); t.source.textured=1;
    for(unsigned i=0;i<3;i++) t.source.colors[i]=0x808080;
    Put16(pixels,0x8000u|16u|(8u<<5)|(4u<<10));
    const u8 expected[4][3]={{116,71,44},{233,143,88},{0,11,22},{134,93,63}};
    for(unsigned mode=0;mode<4;mode++) {
        t.source.texture.tpage=(u16)(256+mode*32);
        t.source.textureBlend=NATIVE_TEXTURE_BLEND_SEMI;
        CHECK(NativeMaterial_TriangleCode(1,t.source.texture.tpage,t.source.textureBlend)==0x36);
        CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
        CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.blended==6);
        CHECK(memcmp(rgb,expected[mode],3)==0 && depth[0]==UINT32_MAX);
        // Non-STP texels retain opaque depth writes for every blend mode.
        Put16(pixels,16u|(8u<<5)|(4u<<10));
        CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
        CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.blended==0);
        CHECK(rgb[0]==132 && rgb[1]==66 && rgb[2]==33 && depth[0]==100);
        Put16(pixels,0x8000u|16u|(8u<<5)|(4u<<10));
    }
    // Ordinary CTR page marker 3 is opaque; explicit SEMI enables quarter-source.
    t.source.textureBlend=NATIVE_TEXTURE_BLEND_AUTO;
    CHECK(NativeMaterial_TriangleCode(1,t.source.texture.tpage,t.source.textureBlend)==0x34);
    CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.blended==0 && rgb[0]==132 && depth[0]==100);
    t.source.texture.tpage=256; t.source.textureBlend=NATIVE_TEXTURE_BLEND_OPAQUE;
    CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.blended==0 && rgb[0]==132);
    // Mixed STP/non-STP samples in one primitive update depth independently.
    t.source.textureBlend=NATIVE_TEXTURE_BLEND_AUTO; t.source.texture.u[1]=4;
    Put16(pixels,31); for(unsigned i=1;i<4;i++) Put16(pixels+2*i,0x801f);
    CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==6 && stats.blended==3);
    CHECK(rgb[0]==255 && depth[0]==100 && rgb[3]==178 && depth[1]==UINT32_MAX);
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==3 && stats.blended==3 && rgb[3]==216);
    // An opaque foreground occludes blended fragments; nearer blends leave its depth.
    t.source.texture.u[1]=0; Put16(pixels,0x801f);
    for(size_t i=0;i<16;i++) depth[i]=50;
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==0);
    for(unsigned i=0;i<3;i++) t.depth[i]=25;
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.blended==6 && depth[0]==50);
    // Addition saturates and subtraction clamps; invisible zero never blends.
    CHECK(NativeMaterial_BlendChannel(240,240,32)==255 && NativeMaterial_BlendChannel(1,255,64)==0);
    Put16(pixels,0);
    CHECK(NativeRaster_Clear(&target,background)==NATIVE_ASSET_OK);
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.transparent==6 && stats.blended==0 && memcmp(rgb,background,3)==0);
    t.source.textureBlend=(enum NativeTextureBlendPolicy)3;
    CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_INVALID_ARGUMENT && stats.written==0);
    free(pixels); return 0;
}
int main(void)
{
	if(TestCoverageAndDepth() || TestTextureAndAtomicity() || TestMaterialBlending()) return 1;
	puts("Native raster: top-left coverage, winding/scissor, affine RGB/UV/depth, transparency/STP, occlusion and transactional texture errors passed."); return 0;
}
