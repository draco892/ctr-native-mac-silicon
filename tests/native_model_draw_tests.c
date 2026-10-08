#include <platform/native_model_draw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while(0)
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static void Fixture(u8 *a,u8 *ptr)
{
	memset(a,0,320); Put16(a+18,1); CTR_WriteU32LE(a+20,24);
	CTR_WriteU32LE(a+56,88); CTR_WriteU32LE(a+60,128); CTR_WriteU32LE(a+64,300); CTR_WriteU32LE(a+68,288);
	CTR_WriteU32LE(a+76,1); CTR_WriteU32LE(a+80,168); CTR_WriteU32LE(a+168,172);
	CTR_WriteU32LE(a+88,1); CTR_WriteU32LE(a+92,0x88010001); CTR_WriteU32LE(a+96,0x00020200);
	CTR_WriteU32LE(a+100,0x00030400); CTR_WriteU32LE(a+104,0x40040000); CTR_WriteU32LE(a+108,UINT32_MAX);
	CTR_WriteU32LE(a+152,28);
	const u8 vertices[]={0,0,0,16,0,0,0,0,16,16,0,16}; memcpy(a+156,vertices,12);
	Put16(a+188,0x8003); Put16(a+190,40); CTR_WriteU32LE(a+220,28); CTR_WriteU32LE(a+260,28);
	memcpy(a+224,vertices,12); memcpy(a+264,vertices,12);
	for(unsigned i=0;i<4;i++) a[264+i*3]+=16;
	CTR_WriteU32LE(a+288,0x112233); CTR_WriteU32LE(a+292,0x445566); CTR_WriteU32LE(a+296,0x778899);
	CTR_WriteU32LE(a+300,304); a[308]=1; a[313]=1; Put16(a+310,256);
	const u32 slots[]={20,56,60,64,68,80,168,300}; CTR_WriteU32LE(ptr,32);
	for(unsigned i=0;i<8;i++) CTR_WriteU32LE(ptr+4+i*4,slots[i]);
}
static int TestPipeline(void)
{
	u8 storage[321],*a=storage+1,ptr[36],copy[320]; struct NativePtrMapEntry entries[8]; struct NativePtrMapView map;
	struct NativeModelView model; struct NativeModelDraw draw,before; struct NativeDrawTriangle t;
	struct NativeModelVertex current[4],next[4]; struct NativePackedModelVertex packed[4];
	struct NativeModelDrawWorkspace workspace={current,next,packed,4};
	const struct NativeProjectionConfig projection={.rotation={.m={{4096,0,0},{0,4096,0},{0,0,4096}}},.translation={0,0,4096},.offset={160*65536,120*65536},.h=4096};
	u8 *pixels=calloc(1,NATIVE_VRAM_BYTES); CHECK(pixels!=NULL); struct NativeVramView vram;
	CHECK(NativeVram_Bind(pixels,NATIVE_VRAM_BYTES,&vram)==NATIVE_ASSET_OK);
	Put16(pixels,0x001f); Put16(pixels+2,0x03e0); Put16(pixels+2048,0x8000);
	Fixture(a,ptr); memcpy(copy,a,320);
	CHECK(NativePtrMap_Decode(a,320,ptr,36,entries,8,&map)==NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map,0,&model)==NATIVE_ASSET_OK);
	CHECK(NativeModelDraw_Open(&model,0,UINT32_MAX,0,&projection,&workspace,&vram,&draw)==NATIVE_ASSET_OK && draw.vertexCount==4);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_OK);
	CHECK(t.source.vertices[0]==0 && t.source.vertices[1]==1 && t.source.vertices[2]==2);
	CHECK(t.screen[0][0]==160 && t.screen[0][1]==120 && t.screen[1][0]==224 && t.screen[2][1]==184);
	CHECK(t.depth[0]==4096 && t.averageDepth==4096 && t.signedArea==4096 && t.projectionFlags==0);
	CHECK(t.source.colors[0]==0x112233 && t.source.colors[2]==0x778899 && t.hasCornerPixels);
	CHECK(t.corners[0].r==255 && t.corners[1].g==255 && t.corners[2].word==0x8000 && t.corners[2].stp && t.corners[2].a==255);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_OK);
	CHECK(t.source.vertices[0]==0 && t.source.vertices[1]==2 && t.source.vertices[2]==3 && t.signedArea==-4096 && !t.hasCornerPixels);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_NOT_FOUND && t.source.command==0);
	before=draw; CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_NOT_FOUND && memcmp(&before,&draw,sizeof(draw))==0);
	CHECK(NativeModelDraw_Open(&model,0,0,1,&projection,&workspace,NULL,&draw)==NATIVE_ASSET_OK);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_OK && t.screen[0][0]==192 && t.signedArea==4096 && !t.hasCornerPixels);
	CHECK(NativeModelDraw_Open(&model,0,0,UINT32_MAX,&projection,&workspace,NULL,&draw)==NATIVE_ASSET_OK);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_OK && t.screen[0][0]==224);
	CHECK(memcmp(a,copy,320)==0);
	// Capacity and absent next-scratch failures publish no iterator.
	workspace.capacity=3;
	CHECK(NativeModelDraw_Open(&model,0,UINT32_MAX,0,&projection,&workspace,NULL,&draw)==NATIVE_ASSET_OUTPUT_TOO_SMALL && draw.commands.map==NULL);
	workspace.capacity=4; workspace.next=NULL;
	CHECK(NativeModelDraw_Open(&model,0,0,1,&projection,&workspace,NULL,&draw)==NATIVE_ASSET_INVALID_ARGUMENT && draw.commands.map==NULL);
	workspace.next=next;
	CHECK(NativePtrMap_Rebind(&map,copy,320)==NATIVE_PTRMAP_OK); CHECK(NativeModel_Open(&map,0,&model)==NATIVE_ASSET_OK);
	CHECK(NativeModelDraw_Open(&model,0,UINT32_MAX,0,&projection,&workspace,&vram,&draw)==NATIVE_ASSET_OK);
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_OK && t.signedArea==4096);
	// Texture address fails after valid commands and projection: all state rolls back.
	Put16(copy+310,271); copy[308]=64;
	CHECK(NativeModelDraw_Open(&model,0,UINT32_MAX,0,&projection,&workspace,&vram,&draw)==NATIVE_ASSET_OK); before=draw;
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_INVALID_DATA && t.source.command==0 && t.hasCornerPixels==0 && memcmp(&before,&draw,sizeof(draw))==0);
	// Invalid cache use is rejected by the connected command path.
	CTR_WriteU32LE(copy+92,0x84010001);
	CHECK(NativeModelDraw_Open(&model,0,UINT32_MAX,0,&projection,&workspace,NULL,&draw)==NATIVE_ASSET_OK); before=draw;
	CHECK(NativeModelDraw_Next(&draw,&t)==NATIVE_ASSET_INVALID_DATA && memcmp(&before,&draw,sizeof(draw))==0);
	CHECK(NativeModelDraw_Next(NULL,&t)==NATIVE_ASSET_INVALID_ARGUMENT);
	free(pixels); return 0;
}
int main(void)
{
	if(TestPipeline()) return 1;
	puts("Native draw pipeline: static/halfway/clamped frames, strip continuation, projected triangles, colors/texels, rebind and transactional failures passed."); return 0;
}
