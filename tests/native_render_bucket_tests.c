#include <platform/native_resident.h>
#include <namespace_Proc.h>
#include <namespace_Display.h>
#include <namespace_RenderTires.h>
#include <ctr_gte.h>
#include <prim.h>
#include <gpu.h>
#include <psx/gtereg.h>
#define COMMON_H
u8 *gCTRNativeScratchpadBase;
static struct
{
	u32 trigApprox[1024];
} data;
#include "../game/MATH/MATH_7_MatrixStubs.c"
#include "../game/MATH/TRIG_AngleSinCos.c"
#include "../game/RenderBucket/RenderBucket_QueueExecute.c"
#include "../game/DrawTires.c"
#include "../game/DrawSky.c"
#include "../game/RenderWeather/RenderWeather.c"
#include "../game/CTR/CTR_CycleTex.c"
#define CHECK(c)                                                 \
	do                                                           \
	{                                                            \
		if (!(c))                                                \
		{                                                        \
			fprintf(stderr, "failed: %s at %d\n", #c, __LINE__); \
			return 1;                                            \
		}                                                        \
	} while (0)
static int PrimitivePackets(void)
{
	_Alignas(max_align_t) u8 packets[4096] = {0};
	u32 ot[64];
	for (size_t i = 0; i < 64; i++)
		ot[i] = NATIVE_GPU_LINK_TERMINATOR;
	NativeGpuLinks_Reset();
	CHECK(NativeGpuLinks_RegisterRange(packets, sizeof(packets), NULL));
	CHECK(NativeGpuLinks_RegisterRange(ot, sizeof(ot), NULL));
	struct PrimMem mem = {.start = packets, .cursor = packets, .end = packets + sizeof(packets), .guardEnd = packets + sizeof(packets)};
	struct InstDrawPerPlayer idpp = {0};
	struct Instance *inst = calloc(1, sizeof(*inst) + sizeof(idpp));
	CHECK(inst);
	struct PushBuffer pb = {.rect = {.w = 512, .h = 216}};
	struct RenderBucketDrawContext ctx = {.inst = inst, .idpp = &idpp, .pb = &pb, .primMem = &mem};
	struct RenderBucketSplitVertex v[3] = {{.sxy = 0x0014000a, .sz = 100, .color = 0x00030201, .uv = 0x0201},
	                                       {.sxy = 0x0028001e, .sz = 100, .color = 0x00060504, .uv = 0x0403},
	                                       {.sxy = 0x003c0032, .sz = 100, .color = 0x00090807, .uv = 0x0605}};
	CHECK(RenderBucket_DrawSplitPrimitiveNormalAtOTEntry(&ctx, 0, NULL, &ot[7], &v[0], &v[1], &v[2]) == 0);
	POLY_G3 *g3 = (void *)packets;
	CHECK(CTR_ReadU32LE(&g3->r0) == 0x30030201 && g3->x1 == 30 && g3->y2 == 60);
	CHECK(g3->tag == 0x06ffffff && NativeGpuLinks_ToHostPointer(ot[7]) == g3 && mem.cursor == packets + 0x1c);
	struct TextureLayout tex = {.clut = 0x1234, .tpage = 0x60};
	CHECK(RenderBucket_DrawSplitPrimitiveNormalAtOTEntry(&ctx, 0, &tex, &ot[7], &v[0], &v[1], &v[2]) == 0);
	POLY_GT3 *gt3 = (void *)(packets + 0x1c);
	CHECK(CTR_ReadU32LE(&gt3->r0) == 0x34030201 && gt3->clut == 0x1234 && gt3->tpage == 0x60);
	CHECK(gt3->u0 == 1 && gt3->v2 == 6 && NativeGpuLinks_ToHostPointer(gt3->tag & 0xffffff) == g3);
	CHECK(NativeGpuLinks_ToHostPointer(ot[7]) == gt3);
	// Full-width ranges clamp before linking, including reflected tire packets.
	struct DrawTiresScratch tires = {.otRangeStart = (uintptr_t)&ot[20], .otRangeEnd = (uintptr_t)&ot[25]};
	POLY_FT4 *ft4 = (void *)(packets + 128);
	DrawTiresSolid_LinkPrimitive(&tires, ft4, (uintptr_t)&ot[10]);
	CHECK(NativeGpuLinks_ToHostPointer(ot[20]) == ft4);
	DrawTiresReflection_LinkPrimitive(&tires, ft4 + 1, (uintptr_t)&ot[30]);
	CHECK(NativeGpuLinks_ToHostPointer(ot[25]) == ft4 + 1);
	struct DB db = {.otMem = {.start = ot, .cursor = ot + 64}};
	CHECK(CtrGpu_IsCurrentOTRange(&db, ot + 20, ot + 25));
	CHECK(!CtrGpu_IsCurrentOTRange(&db, ot + 25, ot + 20));
	struct RenderBucketQueueState queue = {.otCurr = ot + 2, .otEndMinusOne = ot + 63};
	CHECK(RenderBucket_AddressSub(queue.otEndMinusOne, queue.otCurr) == 61 * 4);
	CHECK(RenderBucket_AddressSubOffset(ot + 20, -4) == (uintptr_t)&ot[21]);
	inst->reflectionRGBA = 0x00ffffff;
	inst->specLightX = 1;
	CHECK(RenderBucket_MirrorSpecialPackedXY(&ctx, 0x00100020) == 0xfff00020);
	// FAM lookup must use its argument, independently of a local 'mf'.
	struct
	{
		struct ModelFrame frame;
		u8 verts[4];
	} frame = {.frame = {.vertexOffset = sizeof(struct ModelFrame)}};
	CHECK(MODELFRAME_GETVERT(&frame.frame) == (char *)frame.verts);
	free(inst);
	return 0;
}
static int WorkAndTextureCycles(void)
{
	_Alignas(max_align_t) u8 retail[1024];
	memset(retail, 0xa5, sizeof(retail));
	gCTRNativeScratchpadBase = retail;
	NativeHostScratch_Reset();
	for (u16 i = 0; i < 256; i++)
		*RenderBucket_PackedVertexScratch(i) = (struct RenderBucketPackedVertex){i, i + 1u};
	CHECK(RenderBucket_PackedVertexScratch(255)->z == 256);
	size_t bytes = NativeHostScratch_StorageSize();
	void *snapshot = malloc(bytes);
	CHECK(snapshot);
	memcpy(snapshot, NativeHostScratch_Storage(), bytes);
	NativeHostScratch_Reset();
	memcpy(NativeHostScratch_Storage(), snapshot, bytes);
	free(snapshot);
	CHECK(RenderBucket_PackedVertexScratch(255)->xy == 255);
	for (size_t i = 0; i < sizeof(retail); i++)
		CHECK(retail[i] == 0xa5);
	enum
	{
		COUNT = 3
	};
	size_t record = sizeof(struct AnimTex) + COUNT * sizeof(void *);
	u8 *storage = calloc(1, record + sizeof(void *));
	CHECK(storage);
	struct AnimTex *anim = (void *)storage;
	anim->numFrames = COUNT;
	anim->frameOffset = 1;
	anim->frameSkip = 1;
	struct IconGroup4 icons[COUNT];
	for (unsigned i = 0; i < COUNT; i++)
		ANIMTEX_GETARRAY(anim)[i] = &icons[i];
	memcpy(storage + record, &anim, sizeof(anim));
	for (int tick = 0; tick < 128; tick++)
	{
		CTR_CycleTex_LEV(anim, tick);
		CHECK(anim->frameCurr == ((tick + 1) >> 1) % COUNT && anim->ptrActiveTex == &icons[anim->frameCurr]);
		void *slot = NULL;
		anim->ptrActiveTex = &slot;
		CTR_CycleTex_Model(anim, tick);
		CHECK(slot == &icons[anim->frameCurr]);
	}
	CTR_CycleTex_LEV(anim, -3);
	CHECK(anim->frameCurr == 2);
	anim->numFrames = 0;
	CTR_CycleTex_LEV(anim, 0); // invalid count is rejected before division.
	free(storage);
	return 0;
}
static int WeatherCapture(void)
{
    static u8 scratch[1024];static u32 primitives[4096],ot[1024];
    gCTRNativeScratchpadBase=scratch;
    struct PushBuffer camera={0};camera.rect.w=192;camera.rect.h=128;camera.distanceToScreen_PREV=128;camera.ptrOT=ot;
    for(unsigned i=0;i<3;i++) camera.matrix_ViewProj.m[i][i]=4096;
    struct RainBuffer rain={.numParticles_curr=32,.offsetOT=100,.colorRGBA_top=0x808080,.colorRGBA_bottom=0x404040};
    CTR_WriteU32LE((u8 *)&rain+16,0xfff00000u);
    struct PrimMem memory={.cursor=primitives};
    NativeGpuLinks_Reset();CHECK(NativeGpuLinks_RegisterRange(primitives,sizeof(primitives),NULL));CHECK(NativeGpuLinks_RegisterRange(ot,sizeof(ot),NULL));
    NativeVisual_BeginCapture(1,999);RenderWeather(&camera,&memory,&rain,1,1);NativeVisual_EndCapture();
    CHECK(!NativeVisual_GetStats().overflow);
    struct NativeSceneEffect effect;unsigned count=0;
    while(NativeVisual_StagedEffect(count,&effect)) {CHECK(effect.kind==8 && effect.count==2 && effect.bucket==100);count++;}
    CHECK(count>0);NativeVisual_Reset();return 0;
}
static int SkyCapture(void)
{
    _Alignas(max_align_t) unsigned char arena[4096]={0},canonical[4096];
    u32 ot[1024];struct ShortVertex vertices[3]={0};struct SkyboxFace faces[8]={0};
    vertices[0].Position=(SVECTOR){-20,-20,256,0};vertices[1].Position=(SVECTOR){20,-20,256,0};vertices[2].Position=(SVECTOR){0,20,256,0};
    for(unsigned k=0;k<3;k++) vertices[k].Color=(CVECTOR){40,80,120,0};
    struct Skybox sky={.numVertex=3,.ptrVertex=vertices};
    for(unsigned k=0;k<8;k++) {faces[k].B=sizeof(vertices[0]);faces[k].C=2*sizeof(vertices[0]);sky.numFaces[k]=1;sky.ptrFaces[k]=&faces[k];}
    struct PushBuffer pb={.rect={.w=192,.h=128},.distanceToScreen_PREV=128,.ptrOT=ot};
    for(unsigned k=0;k<3;k++) pb.matrix_ViewProj.m[k][k]=4096;
    size_t canonicalBytes=0;
    for(unsigned capture=0;capture<2;capture++) {
        memset(arena,0,sizeof(arena));for(unsigned k=0;k<1024;k++) ot[k]=NATIVE_GPU_LINK_TERMINATOR;
        NativeGpuLinks_Reset();CHECK(NativeGpuLinks_RegisterRange(arena,sizeof(arena),NULL));CHECK(NativeGpuLinks_RegisterRange(ot,sizeof(ot),NULL));
        struct PrimMem memory={.cursor=arena};
        NativeVisual_BeginCapture(capture,1000);gte_SetGeomScreen(128);gte_SetGeomOffset(96,64);
        DrawSky_Full(&sky,&pb,&memory);NativeVisual_EndCapture();
        size_t bytes=(unsigned char *)memory.cursor-arena;
        CHECK(bytes==4*sizeof(POLY_G3));
        if(!capture) {canonicalBytes=bytes;memcpy(canonical,arena,bytes);}
        else {
            CHECK(bytes==canonicalBytes && memcmp(arena,canonical,bytes)==0 && !NativeVisual_GetStats().overflow);
            struct NativeSceneEffect effect;unsigned count=0;
            while(NativeVisual_StagedEffect(count,&effect)) {CHECK(effect.kind==10 && effect.count==3 && effect.eye[0][2]==256);count++;}
            CHECK(count==8);
            for(unsigned k=0;k<4;k++) CHECK(NativeVisual_EffectCovered((POLY_G3 *)arena+k));
        }
    }
    NativeVisual_Reset();return 0;
}
int main(void)
{
    CHECK(WeatherCapture()==0);
    CHECK(SkyCapture()==0);
	if (PrimitivePackets() || WorkAndTextureCycles())
		return 1;
	puts("Production RenderBucket/tire packets, full-width OT ranges, 256 cached vertices and texture cycles OK");
	return 0;
}
