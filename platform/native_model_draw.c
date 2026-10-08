#include <platform/native_model_draw.h>
#include <string.h>

enum NativeAssetResult NativeModelDraw_Open(const struct NativeModelView *model,u32 headerIndex,
    u32 animationIndex,u32 logicalIndex,const struct NativeProjectionConfig *projection,
    const struct NativeModelDrawWorkspace *workspace,const struct NativeVramView *vram,
    struct NativeModelDraw *out)
{
	struct NativeModelDraw result={0};
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(projection==NULL || workspace==NULL || (vram!=NULL && vram->bytes==NULL)) return NATIVE_ASSET_INVALID_ARGUMENT;
	enum NativeAssetResult status=NativeModelCommands_Open(model,headerIndex,&result.commands);
	if(status!=NATIVE_ASSET_OK) return status;
	status=animationIndex==UINT32_MAX ?
	    NativeModel_PackStaticVertices(model,headerIndex,workspace->current,workspace->packed,workspace->capacity,&result.vertexCount) :
	    NativeModel_PackAnimationVertices(model,headerIndex,animationIndex,logicalIndex,workspace->current,workspace->next,
	        workspace->packed,workspace->capacity,&result.vertexCount);
	if(status!=NATIVE_ASSET_OK) return status;
	result.projection=*projection; result.packed=workspace->packed;
	if(vram!=NULL) result.vram=*vram;
	*out=result; return NATIVE_ASSET_OK;
}
enum NativeAssetResult NativeModelDraw_Next(struct NativeModelDraw *draw,struct NativeDrawTriangle *out)
{
	if(out==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	memset(out,0,sizeof(*out));
	if(draw==NULL || draw->commands.map==NULL) return NATIVE_ASSET_INVALID_ARGUMENT;
	struct NativeModelDraw pending=*draw;
	struct NativeDrawTriangle triangle={0};
	enum NativeAssetResult status=NativeModelCommands_Next(&pending.commands,&triangle.source);
	if(status==NATIVE_ASSET_NOT_FOUND) { *draw=pending; return status; }
	if(status!=NATIVE_ASSET_OK) return status;
	struct NativePackedModelVertex vertices[3];
	for(unsigned i=0;i<3;i++)
	{
		if(triangle.source.vertices[i]>=pending.vertexCount || pending.packed==NULL) return NATIVE_ASSET_INVALID_DATA;
		vertices[i]=pending.packed[triangle.source.vertices[i]];
	}
	struct NativeProjectionState state={0}; struct NativeProjectionResult projected;
	status=NativeModelProjection_Project3(&pending.projection,vertices,&state,&projected);
	if(status!=NATIVE_ASSET_OK) return status;
	memcpy(triangle.screen,state.screen,sizeof(triangle.screen));
	for(unsigned i=0;i<3;i++) triangle.depth[i]=state.depth[i+1];
	triangle.projectionFlags=projected.flags;
	s64 x1=(s64)triangle.screen[1][0]-triangle.screen[0][0],y1=(s64)triangle.screen[1][1]-triangle.screen[0][1];
	s64 x2=(s64)triangle.screen[2][0]-triangle.screen[0][0],y2=(s64)triangle.screen[2][1]-triangle.screen[0][1];
	triangle.signedArea=x1*y2-y1*x2;
	triangle.averageDepth=(u16)(((u32)triangle.depth[0]+triangle.depth[1]+triangle.depth[2])/3);
	if(pending.vram.bytes!=NULL && triangle.source.textured)
	{
		for(unsigned i=0;i<3;i++)
		{
			status=NativeVram_Sample(&pending.vram,triangle.source.texture.tpage,triangle.source.texture.clut,
			    triangle.source.texture.u[i],triangle.source.texture.v[i],&triangle.corners[i]);
			if(status!=NATIVE_ASSET_OK) return status;
		}
		triangle.hasCornerPixels=1;
	}
	*draw=pending; *out=triangle; return NATIVE_ASSET_OK;
}
