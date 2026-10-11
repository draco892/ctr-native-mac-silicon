#include <platform/native_vram.h>
#include <string.h>

enum NativeAssetResult NativeVram_Bind(void *bytes,size_t size,struct NativeVramView *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(bytes==NULL || size<NATIVE_VRAM_BYTES || (uintptr_t)bytes>UINTPTR_MAX-NATIVE_VRAM_BYTES)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	out->bytes=bytes; return NATIVE_ASSET_OK;
}
static enum NativeAssetResult NativeVram_Rect(const struct NativeVramView *vram,const u8 *data,
    size_t size,int write,struct NativeVramLoadInfo *info)
{
	if(size<20) return NATIVE_ASSET_INVALID_DATA;
	u32 x=CTR_ReadU16LE(data+12),y=CTR_ReadU16LE(data+14),w=CTR_ReadU16LE(data+16),h=CTR_ReadU16LE(data+18);
	if(w==0 || h==0 || x>=1024 || y>=512 || w>1024-x || h>512-y) return NATIVE_ASSET_INVALID_DATA;
	size_t words=(size_t)w*h;
	if(words>(size-20)/2) return NATIVE_ASSET_INVALID_DATA;
	if(write) for(u32 row=0;row<h;row++)
		memcpy(vram->bytes+((y+row)*1024+x)*2,data+20+(size_t)row*w*2,w*2);
	info->rectangles++; info->words+=words;
	return NATIVE_ASSET_OK;
}
static enum NativeAssetResult NativeVram_Pass(const struct NativeVramView *vram,const u8 *data,
    size_t bytes,int write,struct NativeVramLoadInfo *info)
{
	if(bytes<4) return NATIVE_ASSET_INVALID_DATA;
	if(CTR_ReadU32LE(data)!=0x20) return NativeVram_Rect(vram,data,bytes,write,info);
	size_t cursor=4;
	while(cursor<=bytes && bytes-cursor>=4)
	{
		u32 size=CTR_ReadU32LE(data+cursor);
		cursor+=4;
		if(size==0) return NATIVE_ASSET_OK;
		size_t aligned=size&~3u;
		if(aligned>bytes-cursor) return NATIVE_ASSET_INVALID_DATA;
		enum NativeAssetResult status=NativeVram_Rect(vram,data+cursor,aligned,write,info);
		if(status!=NATIVE_ASSET_OK) return status;
		cursor+=aligned;
	}
	return NATIVE_ASSET_INVALID_DATA;
}
enum NativeAssetResult NativeVram_Load(const struct NativeVramView *vram,const void *asset,
    size_t bytes,struct NativeVramLoadInfo *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(vram==NULL || vram->bytes==NULL || asset==NULL || (uintptr_t)asset>UINTPTR_MAX-bytes)
		return NATIVE_ASSET_INVALID_ARGUMENT;
	uintptr_t source=(uintptr_t)asset,dest=(uintptr_t)vram->bytes;
	if(source<dest+NATIVE_VRAM_BYTES && dest<source+bytes) return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeVramLoadInfo validated={0},written={0};
	enum NativeAssetResult status=NativeVram_Pass(vram,asset,bytes,0,&validated);
	if(status!=NATIVE_ASSET_OK) return status;
	status=NativeVram_Pass(vram,asset,bytes,1,&written);
	if(status==NATIVE_ASSET_OK) *out=written;
	return status;
}
static enum NativeAssetResult NativeVram_Word(const struct NativeVramView *vram,u32 x,u32 y,u16 *out)
{
	if(x>=1024 || y>=512) return NATIVE_ASSET_INVALID_DATA;
	*out=CTR_ReadU16LE(vram->bytes+(y*1024+x)*2); return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeVram_Sample(const struct NativeVramView *vram,u16 tpage,u16 clut,
    u8 u,u8 v,struct NativeTexturePixel *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(vram==NULL || vram->bytes==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	u32 mode=(tpage>>7)&3, pageX=(tpage&15)*64,pageY=((tpage>>4)&1)*256;
	if(mode==3) mode=2;
	u32 x=pageX+(mode==0 ? u/4 : mode==1 ? u/2 : u),y=pageY+v;
	u16 word;
	enum NativeAssetResult status=NativeVram_Word(vram,x,y,&word);
	if(status!=NATIVE_ASSET_OK) return status;
	if(mode<2)
	{
		u32 index=mode==0 ? (word>>((u%4)*4))&15 : (word>>((u%2)*8))&255;
		status=NativeVram_Word(vram,((clut&63)*16)+index,clut>>6,&word);
		if(status!=NATIVE_ASSET_OK) return status;
	}
	out->word=word; out->stp=(u8)(word>>15); out->a=word==0 ? 0 : 255;
	u8 r=word&31,g=(word>>5)&31,b=(word>>10)&31;
	out->r=(u8)((r<<3)|(r>>2)); out->g=(u8)((g<<3)|(g>>2)); out->b=(u8)((b<<3)|(b>>2));
	return NATIVE_ASSET_OK;
}
