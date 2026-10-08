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
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==6 && depth[0]==100 && rgb[0]==0);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK); Put16(pixels,31);
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && stats.written==6 && rgb[0]==255 && rgb[1]==0);
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK);
	Put16(pixels+2,0x03e0); t.source.texture.u[1]=4;
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_OK && rgb[0]==255 && rgb[3]==0 && rgb[4]==255);
	// A valid early fragment followed by an invalid palette address writes nothing.
	CHECK(NativeRaster_Clear(&target,black)==NATIVE_ASSET_OK); memset(pixels,0,NATIVE_VRAM_BYTES);
	t.source.texture.tpage=128; t.source.texture.clut=63;
	Put16(pixels+2,255); Put16(pixels+1008*2,31);
	memcpy(copy,rgb,48); memcpy(depthCopy,depth,sizeof(depth));
	CHECK(NativeRaster_Draw(&target,&t,&vram,&stats)==NATIVE_ASSET_INVALID_DATA && stats.written==0);
	CHECK(memcmp(copy,rgb,48)==0 && memcmp(depthCopy,depth,sizeof(depth))==0);
	CHECK(NativeRaster_Draw(&target,&t,NULL,&stats)==NATIVE_ASSET_INVALID_ARGUMENT);
	free(pixels); return 0;
}
int main(void)
{
	if(TestCoverageAndDepth() || TestTextureAndAtomicity()) return 1;
	puts("Native raster: top-left coverage, winding/scissor, affine RGB/UV/depth, transparency/STP, occlusion and transactional texture errors passed."); return 0;
}
