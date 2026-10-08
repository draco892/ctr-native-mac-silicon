#include <platform/native_model_transform.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while (0)
static void Put16(u8 *p, u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static int TestGolden(void)
{
	struct NativeFrameView f={.position={1,2,3}}, n={.position={-2,-3,-4}};
	struct NativeModelVertex v={255,128,127}, a={1,2,3}, b={4,5,6};
	struct NativePackedModelVertex out; s16 pos[3];
	CHECK(NativeModel_PackVertex(&f,&v,0,NULL,NULL,&out)==NATIVE_ASSET_OK);
	CHECK(out.xy==0x02000400 && out.z==524);
	CHECK(NativeModel_PackVertex(&f,&v,1,NULL,NULL,&out)==NATIVE_ASSET_OK);
	CHECK(out.xy==0x02000400 && out.z==524);
	CHECK(NativeModel_UnpackPosition(&out,pos)==NATIVE_ASSET_OK && pos[0]==1024 && pos[1]==512 && pos[2]==524);
	CHECK(NativeModel_PackVertex(&f,&a,0,&n,&b,&out)==NATIVE_ASSET_OK && out.xy==0x00100008 && out.z==12);
	f.position[0]=-32768; f.position[1]=-32768; f.position[2]=-32768; v=(struct NativeModelVertex){0,0,0};
	CHECK(NativeModel_PackVertex(&f,&v,0,NULL,NULL,&out)==NATIVE_ASSET_OK && out.xy==0 && out.z==0xfffe0000);
	CHECK(NativeModel_PackVertex(NULL,&v,0,NULL,NULL,&out)==NATIVE_ASSET_INVALID_ARGUMENT && out.xy==0 && out.z==0);
	CHECK(NativeModel_PackVertex(&f,&v,2,NULL,NULL,&out)==NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(NativeModel_PackVertex(&f,&v,0,&n,NULL,&out)==NATIVE_ASSET_INVALID_ARGUMENT);
	CHECK(NativeModel_UnpackPosition(NULL,pos)==NATIVE_ASSET_INVALID_ARGUMENT && pos[0]==0 && pos[1]==0 && pos[2]==0);
	return 0;
}
// Regression: high-bit X must not sign-extend into the packed Z half.
// High-bit vertical bytes must also remain positive before signed frame origin.
static int TestStoredByteBoundary(void)
{
	const struct NativeFrameView frame={0};
	const struct NativeModelVertex vertices[]={{127,127,17},{128,128,17},{255,255,128}};
	const u32 xy[]={0x004001fc,0x00400200,0x020003fc}, z[]={508,512,1020};
	for(unsigned i=0;i<3;i++)
	{
		struct NativePackedModelVertex raw,compressed;
		CHECK(NativeModel_PackVertex(&frame,&vertices[i],0,NULL,NULL,&raw)==NATIVE_ASSET_OK);
		CHECK(NativeModel_PackVertex(&frame,&vertices[i],1,NULL,NULL,&compressed)==NATIVE_ASSET_OK);
		CHECK(compressed.xy==xy[i] && compressed.z==z[i]);
		CHECK(raw.xy==compressed.xy && raw.z==compressed.z);
	}
	return 0;
}
// Independent wide-integer oracle: explicit packed halves, modulo arithmetic
// and multiplication instead of the production packed bitwise-add/shift path.
static unsigned long long Word(const struct NativeModelVertex *v)
{
	return v->z*65536ull+v->x;
}
static int TestDifferential(void)
{
	u32 random=0xbb67ae85;
	for(unsigned trial=0;trial<10000;trial++)
	{
		struct NativeFrameView f={0},n={0}; struct NativeModelVertex v,b; struct NativePackedModelVertex out;
		u8 *vbytes=(u8 *)&v,*bbytes=(u8 *)&b;
		for(unsigned axis=0;axis<3;axis++)
		{
			random=random*1664525u+1013904223u; f.position[axis]=(s16)((int)(random%65536)-32768); vbytes[axis]=(u8)(random>>24);
			random=random*1664525u+1013904223u; n.position[axis]=(s16)((int)(random%65536)-32768); bbytes[axis]=(u8)(random>>24);
		}
		int compressed=trial%2, interpolate=(trial/2)%2;
		int x=f.position[0],y=f.position[1],z=f.position[2]+v.y;
		unsigned long long packed=Word(&v);
		if(interpolate) { x+=n.position[0]; y+=n.position[1]; z+=n.position[2]+b.y; packed+=Word(&b); }
		unsigned low=interpolate ? (unsigned)((x+65536)%65536) : (unsigned)(x+65536)%32768;
		unsigned high=(unsigned)((y+65536)%65536);
		unsigned multiplier=interpolate ? 2 : 4;
		u32 expectedXY=(u32)((packed+high*65536ull+low)*multiplier)&0xfff8ffffu;
		u32 expectedZ=(u32)((long long)z*multiplier);
		CHECK(NativeModel_PackVertex(&f,&v,compressed,interpolate?&n:NULL,interpolate?&b:NULL,&out)==NATIVE_ASSET_OK);
		CHECK(out.xy==expectedXY && out.z==expectedZ);
	}
	return 0;
}
static int TestIntegration(void)
{
	u8 storage[321],*a=storage+1,ptr[32],copy[320];
	const u32 slots[]={20,56,60,80,168}; struct NativePtrMapEntry entries[7]; struct NativePtrMapView map;
	struct NativeModelView model; struct NativeModelVertex curr[2],next[2]; struct NativePackedModelVertex packed[2]; u32 count;
	memset(a,0,320); Put16(a+18,1); CTR_WriteU32LE(a+20,24); CTR_WriteU32LE(a+56,88); CTR_WriteU32LE(a+60,128);
	CTR_WriteU32LE(a+76,1); CTR_WriteU32LE(a+80,168); CTR_WriteU32LE(a+168,172);
	CTR_WriteU32LE(a+92,0x80000000); CTR_WriteU32LE(a+96,0x00010000); CTR_WriteU32LE(a+100,UINT32_MAX);
	Put16(a+128,1); Put16(a+130,2); Put16(a+132,3); CTR_WriteU32LE(a+152,28); memcpy(a+156,"\1\2\3\4\5\6",6);
	Put16(a+188,0x8003); Put16(a+190,36); CTR_WriteU32LE(a+220,28); CTR_WriteU32LE(a+256,28);
	memcpy(a+224,"\1\2\3\4\5\6",6); memcpy(a+260,"\4\5\6\7\10\11",6);
	CTR_WriteU32LE(ptr,20); for(unsigned i=0;i<5;i++) CTR_WriteU32LE(ptr+4+i*4,slots[i]);
	CHECK(NativePtrMap_Decode(a,320,ptr,24,entries,5,&map)==NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map,0,&model)==NATIVE_ASSET_OK); memcpy(copy,a,320);
	CHECK(NativeModel_PackStaticVertices(&model,0,curr,packed,2,&count)==NATIVE_ASSET_OK && count==2 && packed[0].xy==0x00100008 && packed[0].z==20);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,0,curr,NULL,packed,2,&count)==NATIVE_ASSET_OK && packed[0].xy==0x00080004 && packed[0].z==8);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,1,curr,next,packed,2,&count)==NATIVE_ASSET_OK && count==2 && packed[0].xy==0x0010000a && packed[0].z==14);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,UINT32_MAX,curr,NULL,packed,2,&count)==NATIVE_ASSET_OK && packed[0].xy==0x00180010 && packed[0].z==20);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,1,curr,NULL,packed,2,&count)==NATIVE_ASSET_INVALID_ARGUMENT && count==0);
	CHECK(NativeModel_PackStaticVertices(&model,0,curr,packed,1,&count)==NATIVE_ASSET_OUTPUT_TOO_SMALL && count==0);
	CHECK(NativeModel_PackStaticVertices(&model,0,NULL,packed,2,&count)==NATIVE_ASSET_INVALID_ARGUMENT && count==0);
	CHECK(memcmp(a,copy,320)==0);
	// Rebind then reopen; packed output retains no borrowed addresses.
	CHECK(NativePtrMap_Rebind(&map,copy,320)==NATIVE_PTRMAP_OK); CHECK(NativeModel_Open(&map,0,&model)==NATIVE_ASSET_OK);
	CHECK(NativeModel_PackStaticVertices(&model,0,curr,packed,2,&count)==NATIVE_ASSET_OK && packed[0].z==20);
	// Delta accumulators wrap into unsigned stored bytes before packing.
	const u32 compressedSlots[]={20,56,60,72,80,168,192};
	CTR_WriteU32LE(copy+72,280); CTR_WriteU32LE(copy+192,280);
	CTR_WriteU32LE(copy+280,0x1ff); CTR_WriteU32LE(copy+284,0x1ff);
	const u32 streams[]={156,224,260};
	for(unsigned i=0;i<3;i++) { CTR_WriteU32LE(copy+streams[i],0xff7f8005); CTR_WriteU32LE(copy+streams[i]+4,0xfa070000); }
	CTR_WriteU32LE(ptr,28); for(unsigned i=0;i<7;i++) CTR_WriteU32LE(ptr+4+i*4,compressedSlots[i]);
	CHECK(NativePtrMap_Decode(copy,320,ptr,32,entries,7,&map)==NATIVE_PTRMAP_OK);
	CHECK(NativeModel_Open(&map,0,&model)==NATIVE_ASSET_OK);
	CHECK(NativeModel_PackStaticVertices(&model,0,curr,packed,2,&count)==NATIVE_ASSET_OK && packed[0].xy==0x02000400 && packed[0].z==524);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,1,curr,next,packed,2,&count)==NATIVE_ASSET_OK && packed[0].xy==0x01f803fc && packed[0].z==512);
	// Even logical count requires the final stored endpoint for odd requests.
	Put16(copy+188,0x8002);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,UINT32_MAX,curr,next,packed,2,&count)==NATIVE_ASSET_OK && count==2);
	// Direct animation requests use no next scratch and clamp normally.
	Put16(copy+188,2);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,UINT32_MAX,curr,NULL,packed,2,&count)==NATIVE_ASSET_OK && count==2);
	Put16(copy+188,0x8003);
	// Malformed next frame must fail even when current frame is valid.
	CTR_WriteU32LE(copy+256,37);
	CHECK(NativeModel_PackAnimationVertices(&model,0,0,1,curr,next,packed,2,&count)==NATIVE_ASSET_INVALID_DATA && count==0);
	return 0;
}
int main(void)
{
	if(TestGolden() || TestStoredByteBoundary() || TestDifferential() || TestIntegration()) return 1;
	puts("Native model transforms: unsigned stored bytes, signed frame origins, halfway interpolation, 10000 wide-integer comparisons and bounded asset integration passed."); return 0;
}
