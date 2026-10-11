#include <platform/native_vram.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while(0)
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static void Rect(u8 *p,u16 x,u16 y,u16 w,u16 h)
{ memset(p,0,24); CTR_WriteU32LE(p,0x10); Put16(p+12,x); Put16(p+14,y); Put16(p+16,w); Put16(p+18,h); }
static int TestLoad(struct NativeVramView *vram)
{
	u8 storage[65],*a=storage+1; struct NativeVramLoadInfo info;
	memset(vram->bytes,0x5a,NATIVE_VRAM_BYTES);
	Rect(a,1022,511,2,1); Put16(a+20,0x1234); Put16(a+22,0xabcd);
	CHECK(NativeVram_Load(vram,a,24,&info)==NATIVE_ASSET_OK && info.rectangles==1 && info.words==2);
	CHECK(CTR_ReadU16LE(vram->bytes+NATIVE_VRAM_BYTES-4)==0x1234 && CTR_ReadU16LE(vram->bytes+NATIVE_VRAM_BYTES-2)==0xabcd);
	CHECK(NativeVram_Load(vram,a,23,&info)==NATIVE_ASSET_INVALID_DATA && info.rectangles==0);
	Put16(a+16,3); CHECK(NativeVram_Load(vram,a,24,&info)==NATIVE_ASSET_INVALID_DATA);
	memset(a,0,64); CTR_WriteU32LE(a,0x20); CTR_WriteU32LE(a+4,27); // low bits discarded by retail.
	Rect(a+8,0,0,2,1); Put16(a+28,0x1111); Put16(a+30,0x2222);
	CTR_WriteU32LE(a+32,24); Rect(a+36,1,0,1,1); Put16(a+56,0x3333);
	CHECK(NativeVram_Load(vram,a,64,&info)==NATIVE_ASSET_OK && info.rectangles==2 && info.words==3);
	CHECK(CTR_ReadU16LE(vram->bytes)==0x1111 && CTR_ReadU16LE(vram->bytes+2)==0x3333);
	memset(vram->bytes,0x5a,NATIVE_VRAM_BYTES);
	Put16(a+52,1024); // Malformed second rectangle: first must never be written.
	CHECK(NativeVram_Load(vram,a,64,&info)==NATIVE_ASSET_INVALID_DATA);
	for(size_t i=0;i<NATIVE_VRAM_BYTES;i++) CHECK(vram->bytes[i]==0x5a);
	Put16(a+52,1); CHECK(NativeVram_Load(vram,a,60,&info)==NATIVE_ASSET_INVALID_DATA); // Missing sentinel.
	CHECK(NativeVram_Load(vram,vram->bytes,24,&info)==NATIVE_ASSET_INVALID_ARGUMENT);
	CTR_WriteU32LE(a+4,UINT32_MAX); CHECK(NativeVram_Load(vram,a,64,&info)==NATIVE_ASSET_INVALID_DATA);
	return 0;
}
static int TestSequentialUploads(struct NativeVramView *vram)
{
	u8 first[24],second[24]; struct NativeVramLoadInfo info;
	memset(vram->bytes,0,NATIVE_VRAM_BYTES);
	Rect(first,0,0,2,1); Put16(first+20,0x1111); Put16(first+22,0x2222);
	Rect(second,1,0,1,1); Put16(second+20,0x3333);
	CHECK(NativeVram_Load(vram,first,sizeof(first),&info)==NATIVE_ASSET_OK);
	CHECK(NativeVram_Load(vram,second,sizeof(second),&info)==NATIVE_ASSET_OK);
	CHECK(CTR_ReadU16LE(vram->bytes)==0x1111 && CTR_ReadU16LE(vram->bytes+2)==0x3333);
	CHECK(CTR_ReadU16LE(vram->bytes+4)==0); // No clear between uploads.
	Put16(second+16,1024); // Failed later file preserves earlier residency.
	CHECK(NativeVram_Load(vram,second,sizeof(second),&info)==NATIVE_ASSET_INVALID_DATA);
	CHECK(CTR_ReadU16LE(vram->bytes)==0x1111 && CTR_ReadU16LE(vram->bytes+2)==0x3333);
	CHECK(NativeVram_Load(vram,first,sizeof(first),&info)==NATIVE_ASSET_OK);
	CHECK(CTR_ReadU16LE(vram->bytes+2)==0x2222); // Reverse order changes overlap.
	return 0;
}
static int TestSample(struct NativeVramView *vram)
{
	memset(vram->bytes,0,NATIVE_VRAM_BYTES); struct NativeTexturePixel p;
	u16 colors[]={0,0x8000,0x001f,0x03e0,0x7c00};
	for(unsigned i=0;i<5;i++) Put16(vram->bytes+(10*1024+32+i)*2,colors[i]);
	Put16(vram->bytes+(256*1024+64)*2,0x3210); u16 clut=(10<<6)|2;
	CHECK(NativeVram_Sample(vram,17,clut,0,0,&p)==NATIVE_ASSET_OK && p.word==0 && p.a==0);
	CHECK(NativeVram_Sample(vram,17,clut,1,0,&p)==NATIVE_ASSET_OK && p.word==0x8000 && p.a==255 && p.stp==1 && p.r==0 && p.g==0 && p.b==0);
	CHECK(NativeVram_Sample(vram,17,clut,2,0,&p)==NATIVE_ASSET_OK && p.r==255 && p.g==0 && p.b==0);
	CHECK(NativeVram_Sample(vram,17,clut,3,0,&p)==NATIVE_ASSET_OK && p.g==255);
	Put16(vram->bytes+(256*1024+64)*2,0x0402);
	CHECK(NativeVram_Sample(vram,145,clut,0,0,&p)==NATIVE_ASSET_OK && p.r==255);
	CHECK(NativeVram_Sample(vram,145,clut,1,0,&p)==NATIVE_ASSET_OK && p.b==255);
	Put16(vram->bytes+(256*1024+64)*2,0x8000);
	CHECK(NativeVram_Sample(vram,273,65535,0,0,&p)==NATIVE_ASSET_OK && p.word==0x8000 && p.a==255);
	CHECK(NativeVram_Sample(vram,401,65535,0,0,&p)==NATIVE_ASSET_OK && p.word==0x8000);
	CHECK(NativeVram_Sample(vram,271,clut,64,0,&p)==NATIVE_ASSET_INVALID_DATA && p.word==0);
	Put16(vram->bytes+(256*1024+64)*2,255);
	CHECK(NativeVram_Sample(vram,145,63,0,0,&p)==NATIVE_ASSET_INVALID_DATA);
	CHECK(NativeVram_Sample(vram,17,65535,0,0,&p)==NATIVE_ASSET_INVALID_DATA);
	return 0;
}
static int TestDifferential(struct NativeVramView *vram)
{
	u32 random=0x9b05688c;
	for(size_t i=0;i<NATIVE_VRAM_BYTES;i++) { random=random*1664525u+1013904223u; vram->bytes[i]=(u8)(random>>24); }
	for(unsigned trial=0;trial<10000;trial++)
	{
		random=random*1664525u+1013904223u; u16 page=(u16)random;
		random=random*1664525u+1013904223u; u16 clut=(u16)random;
		random=random*1664525u+1013904223u; u8 u=(u8)random,v=(u8)(random>>8);
		unsigned mode=page/128%4; if(mode==3) mode=2;
		unsigned x=page%16*64+u/(mode==0 ? 4 : mode==1 ? 2 : 1),y=page/16%2*256+v;
		int valid=x<1024 && y<512; unsigned word=0;
		if(valid)
		{
			size_t offset=(y*1024+x)*2;
			word=vram->bytes[offset]+256u*vram->bytes[offset+1];
			if(mode<2)
			{
				unsigned divisor=1,shift=(mode==0 ? u%4*4 : u%2*8);
				for(unsigned i=0;i<shift;i++) divisor*=2;
				unsigned index=word/divisor%(mode==0 ? 16 : 256);
				x=clut%64*16+index; y=clut/64; valid=x<1024 && y<512;
				if(valid) { offset=(y*1024+x)*2; word=vram->bytes[offset]+256u*vram->bytes[offset+1]; }
			}
		}
		struct NativeTexturePixel p;
		enum NativeAssetResult status=NativeVram_Sample(vram,page,clut,u,v,&p);
		if(!valid) { CHECK(status==NATIVE_ASSET_INVALID_DATA && p.word==0 && p.a==0); continue; }
		CHECK(status==NATIVE_ASSET_OK && p.word==word && p.a==(word==0 ? 0 : 255) && p.stp==word/32768);
		CHECK(p.r==(word%32*8+word%32/4) && p.g==(word/32%32*8+word/32%32/4) && p.b==(word/1024%32*8+word/1024%32/4));
	}
	return 0;
}
int main(void)
{
	u8 *allocation=malloc(NATIVE_VRAM_BYTES+1); CHECK(allocation!=NULL);
	struct NativeVramView vram;
	CHECK(NativeVram_Bind(allocation+1,NATIVE_VRAM_BYTES,&vram)==NATIVE_ASSET_OK);
	CHECK(NativeVram_Bind(allocation,NATIVE_VRAM_BYTES-1,&(struct NativeVramView){0})==NATIVE_ASSET_INVALID_ARGUMENT);
	if(TestLoad(&vram) || TestSequentialUploads(&vram) || TestSample(&vram) || TestDifferential(&vram)) return 1;
	free(allocation); puts("Native VRAM: atomic rect loading, 4/8/16-bit texels, palettes, RGB/STP/alpha, boundaries and 10000 differential samples passed."); return 0;
}
