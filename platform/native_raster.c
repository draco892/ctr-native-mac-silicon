#include <platform/native_raster.h>
#include <string.h>

enum NativeAssetResult NativeRaster_Bind(void *rgb,size_t rgbBytes,u32 *depth,size_t depthCount,
    u32 width,u32 height,struct NativeRasterView *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(rgb==NULL || depth==NULL || width==0 || height==0 || width>4096 || height>4096 ||
	    (uintptr_t)depth%_Alignof(u32)!=0) return NATIVE_ASSET_INVALID_ARGUMENT;
	size_t pixels=(size_t)width*height;
	if(pixels>rgbBytes/3 || pixels>depthCount || (uintptr_t)rgb>UINTPTR_MAX-pixels*3 ||
	    (uintptr_t)depth>UINTPTR_MAX-pixels*sizeof(*depth)) return NATIVE_ASSET_INVALID_ARGUMENT;
	*out=(struct NativeRasterView){rgb,depth,width,height}; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeRaster_Clear(const struct NativeRasterView *target,const u8 background[3])
{
	if(target==NULL || target->rgb==NULL || target->depth==NULL || background==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	for(size_t i=0;i<(size_t)target->width*target->height;i++)
	{ memcpy(target->rgb+i*3,background,3); target->depth[i]=UINT32_MAX; }
	return NATIVE_ASSET_OK;
}
struct NativeRasterPoint { s32 x,y; unsigned index; };
static s64 NativeRaster_Edge(struct NativeRasterPoint a,struct NativeRasterPoint b,s32 x,s32 y)
{ return (s64)(b.x-a.x)*(y-a.y)-(s64)(b.y-a.y)*(x-a.x); }
static int NativeRaster_TopLeft(struct NativeRasterPoint a,struct NativeRasterPoint b)
{ return b.y<a.y || (b.y==a.y && b.x>a.x); }
static u32 NativeRaster_Interpolate(const s64 weights[3],s64 area,const u32 values[3])
{ return (u32)((weights[0]*values[0]+weights[1]*values[1]+weights[2]*values[2])/area); }
static enum NativeAssetResult NativeRaster_Pass(const struct NativeRasterView *target,
    const struct NativeDrawTriangle *triangle,const struct NativeVramView *vram,int write,
    struct NativeRasterStats *stats)
{
	struct NativeRasterPoint p[3];
	s32 minX=32767,maxX=-32768,minY=32767,maxY=-32768;
	for(unsigned i=0;i<3;i++)
	{
		s32 x=triangle->screen[i][0],y=triangle->screen[i][1];
		if(x<minX) minX=x; if(x>maxX) maxX=x;
		if(y<minY) minY=y; if(y>maxY) maxY=y;
		p[i]=(struct NativeRasterPoint){x*2,y*2,i};
		if(triangle->depth[i]==0) { stats->skipped=1; return NATIVE_ASSET_OK; }
	}
	s64 area=NativeRaster_Edge(p[0],p[1],p[2].x,p[2].y);
	if(area==0 || (triangle->projectionFlags&0x60000u) || maxX<0 || maxY<0 ||
	    minX>=(s32)target->width || minY>=(s32)target->height)
	{ stats->skipped=1; return NATIVE_ASSET_OK; }
	if(area<0) { struct NativeRasterPoint temp=p[1]; p[1]=p[2]; p[2]=temp; area=-area; }
	if(minX<0) minX=0; if(minY<0) minY=0;
	if(maxX>=(s32)target->width) maxX=(s32)target->width-1;
	if(maxY>=(s32)target->height) maxY=(s32)target->height-1;
	int inclusive[3]={NativeRaster_TopLeft(p[1],p[2]),NativeRaster_TopLeft(p[2],p[0]),NativeRaster_TopLeft(p[0],p[1])};
	for(s32 y=minY;y<=maxY;y++) for(s32 x=minX;x<=maxX;x++)
	{
		s64 weights[3]={NativeRaster_Edge(p[1],p[2],x*2+1,y*2+1),
		    NativeRaster_Edge(p[2],p[0],x*2+1,y*2+1),NativeRaster_Edge(p[0],p[1],x*2+1,y*2+1)};
		if(weights[0]<0 || weights[1]<0 || weights[2]<0 ||
		    (weights[0]==0 && !inclusive[0]) || (weights[1]==0 && !inclusive[1]) || (weights[2]==0 && !inclusive[2])) continue;
		stats->covered++;
		u32 values[3]; for(unsigned i=0;i<3;i++) values[i]=triangle->depth[p[i].index];
		u32 depth=NativeRaster_Interpolate(weights,area,values);
		size_t pixel=(size_t)y*target->width+(u32)x;
		if(depth>=target->depth[pixel]) continue;
		u8 rgb[3];
		for(unsigned channel=0;channel<3;channel++)
		{
			for(unsigned i=0;i<3;i++) values[i]=(triangle->source.colors[p[i].index]>>(channel*8))&255;
			rgb[channel]=(u8)NativeRaster_Interpolate(weights,area,values);
		}
		int blend=0;
		if(triangle->source.textured)
		{
			for(unsigned i=0;i<3;i++) values[i]=triangle->source.texture.u[p[i].index];
			u8 u=(u8)NativeRaster_Interpolate(weights,area,values);
			for(unsigned i=0;i<3;i++) values[i]=triangle->source.texture.v[p[i].index];
			u8 v=(u8)NativeRaster_Interpolate(weights,area,values);
			struct NativeTexturePixel texel;
			enum NativeAssetResult status=NativeVram_Sample(vram,triangle->source.texture.tpage,triangle->source.texture.clut,u,v,&texel);
			if(status!=NATIVE_ASSET_OK) return status;
			if(texel.a==0) { stats->transparent++; continue; }
			blend=texel.stp && NativeMaterial_IsSemiTransparent(triangle->source.texture.tpage,triangle->source.textureBlend);
			u8 channels[3]={texel.r,texel.g,texel.b};
			for(unsigned channel=0;channel<3;channel++)
			{ u32 modulated=(u32)rgb[channel]*channels[channel]/128; rgb[channel]=(u8)(modulated>255 ? 255 : modulated); }
		}
		if(blend) {
			for(unsigned channel=0;channel<3;channel++) rgb[channel]=NativeMaterial_BlendChannel(
			    target->rgb[pixel*3+channel],rgb[channel],triangle->source.texture.tpage);
			stats->blended++;
		}
		stats->written++;
		if(write) { memcpy(target->rgb+pixel*3,rgb,3); if(!blend) target->depth[pixel]=depth; }
	}
	return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeRaster_Draw(const struct NativeRasterView *target,
    const struct NativeDrawTriangle *triangle,const struct NativeVramView *vram,
    struct NativeRasterStats *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(target==NULL || target->rgb==NULL || target->depth==NULL || triangle==NULL ||
	    (triangle->source.textured && (vram==NULL || vram->bytes==NULL))) return NATIVE_ASSET_INVALID_ARGUMENT;
	if((unsigned)triangle->source.textureBlend>NATIVE_TEXTURE_BLEND_SEMI) return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeRasterStats checked={0},written={0};
	enum NativeAssetResult status=NativeRaster_Pass(target,triangle,vram,0,&checked);
	if(status!=NATIVE_ASSET_OK) return status;
	status=NativeRaster_Pass(target,triangle,vram,1,&written);
	if(status==NATIVE_ASSET_OK) *out=written;
	return status;
}
