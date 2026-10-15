#include <common.h>
#include <platform/native_scene_snapshot_game.h>
#include <platform/native_scene_snapshot.h>
#include <platform/native_scene_layout.h>
#include <platform/native_scene_visibility.h>
#include <platform/native_scene_assets.h>
#include <platform/native_vertex_animation.h>
#include <platform/native_visual.h>
#include <platform/native_renderer_types.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_GAME_PACKETS 16384u
#define SCENE_GAME_TERRAIN_ID UINT64_C(0xe711e711e711e711)
struct SceneGamePacket { u32 words[33];unsigned next; };
static struct NativeSceneSnapshot *sceneGame;
static struct SceneGamePacket scenePackets[SCENE_GAME_PACKETS];
struct SceneGameGroup {unsigned head,tail,view,bucket,depth,order;};
static struct SceneGameGroup sceneGroups[SCENE_GAME_PACKETS];
static unsigned sceneGroupCount;
static unsigned sceneHeads[5][1024],sceneTails[5][1024],scenePacketCount,sceneVertexBudget;
static int sceneCapture,sceneReady,sceneTerrain,sceneWanted,sceneWorldMembership;
static uint64_t sceneEpoch;
static struct GameTracker *sceneTracker;
static unsigned sceneEffects,sceneEffectFallback;
static unsigned sceneRetainedModels,sceneCanonicalReasons;
static struct NativeSceneGameSnapshotStats sceneStats;
#ifdef CTR_INTERNAL
static unsigned sceneCaptureErrors;
static uint64_t sceneErrorSignature;
static void SceneGameCaptureError(const char *name,int result)
{
    if(sceneCaptureErrors++<8) Platform_Log("[CTR Scene Snapshot] capture rejected: %.16s (%d)\n",name,result);
}
#else
#define SceneGameCaptureError(name,result) ((void)0)
#endif

static struct NativeSceneLayout SceneGameLayout(const struct GameTracker *tracker)
{
    struct NativeSceneLayout layout={.views=(unsigned)tracker->numPlyrCurrGame,.ui=tracker->pushBuffer_UI.ptrOT};
    for(unsigned i=0;i<layout.views && i<4;i++) layout.world[i]=tracker->pushBuffer[i].ptrOT;
    return layout;
}
/* Only the normal environment can precede regenerated world geometry.
 * DecalMP may link an offscreen OT into bucket 1023 as well as lower buckets. */
static int SceneGameWorldEnvironment(const struct GameTracker *tracker,unsigned view,const u32 *packet,unsigned length)
{
    if(!tracker->backBuffer || length<3) return 0;
    const struct PushBuffer *pb=&tracker->pushBuffer[view];
    const DRAWENV *env=&tracker->backBuffer->drawEnv;
    int x=env->clip.x+pb->rect.x,y=env->clip.y+pb->rect.y;
    u32 clip=0xe3000000u|((u32)x&1023u)|(((u32)y&1023u)<<10);
    u32 end=0xe4000000u|((u32)(x+pb->rect.w-1)&1023u)|(((u32)(y+pb->rect.h-1)&1023u)<<10);
    u32 offset=0xe5000000u|((u32)(env->ofs[0]+pb->rect.x)&2047u)|(((u32)(env->ofs[1]+pb->rect.y)&2047u)<<11);
    return packet[1]==clip && packet[2]==end && packet[3]==offset;
}
static void SceneGameCaptureInstance(const struct Instance *instance)
{
    if(!instance) return;
    struct NativeSceneObjectSnapshot object={.id=NativeVisual_ObjectID(instance),.flags=instance->flags,
        .animation=instance->animIndex,.frame=(u16)instance->animFrame,.orderingBias=(s8)instance->depthBiasNormal,
        .viewportMask=sceneWorldMembership ? NATIVE_SCENE_WORLD_MEMBERSHIP:0u};
    for(unsigned row=0;row<3;row++) {
        object.pose.position[row]=(float)instance->matrix.t[row];
        for(unsigned col=0;col<3;col++) object.pose.basis[row][col]=instance->matrix.m[row][col]/4096.0f;
    }
    struct NativeModelView model;struct NativeModelHeaderView header;
    int readable=instance->model && NativeSceneAssets_GetModel(&gNativeSceneAssets,instance->model,&model)==NATIVE_ASSET_OK &&
        NativeModel_GetHeader(&model,0,&header)==NATIVE_ASSET_OK;
    if(readable) {
        object.poseIdentity=(uint64_t)(uintptr_t)instance->model->headers;
        object.modelIdentity=object.poseIdentity^((uint64_t)instance->animIndex<<48);
    }
    object.supported=readable && !(header.flags&1) && (instance->flags&DRAW_COLLISION_MASK) && !(instance->flags&HIDE_MODEL) &&
        !(instance->flags&(PIXEL_LOD|SCREENSPACE_INSTANCE|CUSTOM_MATRIX|DRAW_BILLBOARD|DRAW_HUGE|SPLIT_STATE_MASK));
    if(readable && !header.animationCount) object.animation=UINT32_MAX;
    if(object.supported) {
        struct NativeModelMatrix rotation,world;
        memcpy(rotation.m,instance->matrix.m,sizeof(rotation.m));
        if(NativeModelMatrix_Build(&rotation,header.scale,&instance->scale.x,4096,0,&world)!=NATIVE_ASSET_OK) object.supported=0;
        else for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) object.pose.basis[row][col]=world.m[row][col]/4096.0f;
    }
    if(!NativeSceneSnapshot_Object(sceneGame,&object) || !object.supported) return;
    u32 count=0;
    enum NativeAssetResult countResult=NativeModel_GetVertexCount(&model,0,&count);
    if(countResult!=NATIVE_ASSET_OK || count>4096) {SceneGameCaptureError(instance->name,countResult);NativeSceneSnapshot_Invalidate(sceneGame);return;}
    struct NativeModelVertex *scratch=calloc(count?count*2:1,sizeof(*scratch));
    struct NativePackedModelVertex *packed=calloc(count?count:1,sizeof(*packed));
    if(!scratch || !packed) {free(scratch);free(packed);NativeSceneSnapshot_Invalidate(sceneGame);return;}
    enum NativeAssetResult result=object.animation==UINT32_MAX ? NativeModel_PackStaticVertices(&model,0,scratch,packed,count,&count) :
        NativeModel_PackAnimationVertices(&model,0,object.animation,object.frame,scratch,scratch+count,packed,count,&count);
    struct NativeModelCommands commands;
    if(result==NATIVE_ASSET_OK) result=NativeModelCommands_Open(&model,0,&commands);
    size_t first=result==NATIVE_ASSET_OK ? commands.cursor : 0;
    struct NativeModelTriangle material;
    while(result==NATIVE_ASSET_OK && (result=NativeModelCommands_Next(&commands,&material))==NATIVE_ASSET_OK) {
        struct NativeSceneSourceTriangle source={.object=object.id,.primitive=commands.cursor-first,.material=material,
            .orderingBias=(s8)instance->depthBiasNormal};
        if(material.command&0x10000000u) source.cull=(((instance->flags>>15)^(material.command>>29))&1) ? -1 : 1;
        if(instance->flags&DRAW_TRANSPARENT) source.material.textureBlend=NATIVE_TEXTURE_BLEND_SEMI;
        for(unsigned k=0;k<3;k++) {
            if(material.vertices[k]>=count) {result=NATIVE_ASSET_INVALID_DATA;break;}
            const struct NativePackedModelVertex *p=&packed[material.vertices[k]];
            source.local[k][0]=(s16)p->xy;source.local[k][1]=(s16)(p->xy>>16);source.local[k][2]=(s16)p->z;
            if(instance->flags&USE_SPECULAR_LIGHT) {
                u32 color=instance->colorRGBA;
                source.material.colors[k]=((color>>20)&255)|(((color>>12)&255)<<8)|(((color>>4)&255)<<16);
            }
        }
        if(result!=NATIVE_ASSET_OK || !NativeSceneSnapshot_Triangle(sceneGame,&source)) break;
    }
    if(result!=NATIVE_ASSET_NOT_FOUND && result!=NATIVE_ASSET_OK) {SceneGameCaptureError(instance->name,result);NativeSceneSnapshot_Invalidate(sceneGame);}
    free(scratch);free(packed);
}
static void SceneGameCaptureTerrain(const struct Level *legacy,u32 tick)
{
    if(!legacy || !legacy->ptr_mesh_info) return;
    struct NativeLevelView level;struct NativeMeshView mesh,animated;
    if(NativeSceneAssets_GetLevel(&gNativeSceneAssets,legacy,&level)!=NATIVE_ASSET_OK || NativeLevel_GetMesh(&level,&mesh)!=NATIVE_ASSET_OK) {
        NativeSceneSnapshot_Invalidate(sceneGame);return;
    }
    struct NativeVertexAnimationView animation={0};
    enum NativeAssetResult result=NativeVertexAnimation_Open(&level,&mesh,&animation);
    size_t bytes=(size_t)mesh.vertexCount*NATIVE_VERTEX_BYTES;
    void *vertices=animation.count && result==NATIVE_ASSET_OK ? malloc(bytes) : NULL;
    u8 *water=calloc(mesh.quadCount?mesh.quadCount:1,1);
    if(result!=NATIVE_ASSET_OK || (animation.count && !vertices) || !water) {free(vertices);free(water);NativeSceneSnapshot_Invalidate(sceneGame);return;}
    /* Full-scene sources need animation even outside the canonical PVS. This
     * copied view ignores the asset visibility mask without modifying it. */
    struct NativeVertexAnimationView fullAnimation=animation;fullAnimation.visibility=NULL;
    result=NativeVertexAnimation_Apply(&fullAnimation,tick,NULL,NULL,0,vertices,bytes,&animated);
    /* Follow the complete reachable BSP, without the tick's camera/PVS mask.
     * Allocated-but-unreachable nodes/quads are not renderable scene sources. */
    u32 *stack=mesh.bspCount ? malloc(((size_t)mesh.bspCount*2+1)*sizeof(*stack)):NULL;
    u8 *states=mesh.bspCount ? calloc(mesh.bspCount,1):NULL;
    if(mesh.bspCount && (!stack || !states)) result=NATIVE_ASSET_OUTPUT_TOO_SMALL;
    size_t stackCount=result==NATIVE_ASSET_OK && mesh.bspCount ? 1:0;if(stackCount) stack[0]=0;
    if(!mesh.bspCount) memset(water,1,mesh.quadCount);
    while(result==NATIVE_ASSET_OK && stackCount) {
        u32 index=stack[--stackCount],nodeIndex=index&0x3fff;
        if(nodeIndex>=mesh.bspCount) {result=NATIVE_ASSET_INVALID_DATA;break;}
        if(index&0x80000000u) {states[nodeIndex]=2;continue;}
        if(states[nodeIndex]==1) {result=NATIVE_ASSET_INVALID_DATA;break;}if(states[nodeIndex]==2) continue;
        struct NativeSceneBsp node;result=NativeSceneBsp_Get(&level,&mesh,nodeIndex,&node);
        if(result!=NATIVE_ASSET_OK) break;
        if(node.flags&1) {
            states[nodeIndex]=2;
            for(u32 q=0;q<node.quadCount;q++) water[node.firstQuad+q]|=(node.flags&2)?3:1;
        } else {
            states[nodeIndex]=1;stack[stackCount++]=nodeIndex|0x80000000u;
            for(unsigned child=0;child<2;child++) if(!(node.children[child]&0x8000)) stack[stackCount++]=node.children[child]&0x3fff;
        }
    }
    free(stack);free(states);
    struct NativeSceneObjectSnapshot object={.id=SCENE_GAME_TERRAIN_ID,.modelIdentity=(uint64_t)(uintptr_t)legacy,.supported=1,
        .pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    if(result==NATIVE_ASSET_OK && !NativeSceneSnapshot_Object(sceneGame,&object)) result=NATIVE_ASSET_INVALID_DATA;
    for(u32 q=0;result==NATIVE_ASSET_OK && q<animated.quadCount;q++) if(water[q]&1) for(u32 face=0;result==NATIVE_ASSET_OK && face<4;face++)
        for(u32 t=0;result==NATIVE_ASSET_OK && t<2;t++) {
            struct NativeTerrainTriangle triangle;
            result=NativeTerrain_GetTriangle(&level,&animated,q,face,t,2,tick,&triangle);
            if(result==NATIVE_ASSET_INVALID_DATA) result=NativeTerrain_GetTriangle(&level,&animated,q,face,t,1,tick,&triangle);
            if(result==NATIVE_ASSET_INVALID_DATA) result=NativeTerrain_GetTriangle(&level,&animated,q,face,t,0,tick,&triangle);
            if(result==NATIVE_ASSET_NOT_FOUND) {result=NATIVE_ASSET_OK;continue;}
            if(result!=NATIVE_ASSET_OK) break;
            struct NativeSceneSourceTriangle source={.object=object.id,.primitive=((uint64_t)q<<8)|((uint64_t)face<<4)|t,
                .material={.textured=triangle.textured,.texture=triangle.texture},.orderingBias=triangle.orderingBias,
                .cull=triangle.doubleSided ? 0 : (triangle.selectorWord&0x80000000u) ? -1 : 1,.reflectedCull=1};
            if(water[q]&2) {
                void *environment=NULL;
                if(NativePtrMap_Resolve(level.map,0x44,12,&environment)!=NATIVE_PTRMAP_OK) {result=NATIVE_ASSET_INVALID_DATA;break;}
                source.material.textured=1;source.material.textureBlend=NATIVE_TEXTURE_BLEND_SEMI;
                source.continuousUV=1;
                source.material.texture.clut=CTR_ReadU16LE((u8 *)environment+2);source.material.texture.tpage=CTR_ReadU16LE((u8 *)environment+6);
            }
            for(unsigned k=0;k<3;k++) {
                source.material.vertices[k]=triangle.geometry.indices[k];source.material.colors[k]=triangle.geometry.vertices[k].colorHigh;
                for(unsigned axis=0;axis<3;axis++) source.local[k][axis]=triangle.geometry.vertices[k].position[axis];
                if(water[q]&2) {source.material.texture.u[k]=(u8)triangle.geometry.vertices[k].colorLow;source.material.texture.v[k]=(u8)(triangle.geometry.vertices[k].colorLow>>8);}
            }
            if(!NativeSceneSnapshot_Triangle(sceneGame,&source)) {result=NATIVE_ASSET_OUTPUT_TOO_SMALL;break;}
        }
    if(result!=NATIVE_ASSET_OK) {SceneGameCaptureError("terrain",result);NativeSceneSnapshot_Invalidate(sceneGame);}else sceneTerrain=1;
    free(vertices);free(water);
}
void NativeSceneGameSnapshot_Begin(struct GameTracker *tracker,int enabled,uint64_t signature)
{
#ifdef CTR_INTERNAL
    if(sceneErrorSignature!=signature) {sceneCaptureErrors=0;sceneErrorSignature=signature;}
#endif
    sceneEpoch=NativeVisual_ResetEpoch();sceneTracker=tracker;
    sceneEffects=sceneEffectFallback=0;
    sceneRetainedModels=sceneCanonicalReasons=0;
    sceneCapture=sceneReady=sceneTerrain=0;memset(&sceneStats,0,sizeof(sceneStats));
    enabled=enabled && tracker->numPlyrCurrGame>=1 && tracker->numPlyrCurrGame<=4 && !(tracker->gameMode1&LOADING);
    sceneWanted=enabled;
    /* The opening crate changes scripted model visibility and split geometry
     * during preparation. Partial replacement cannot reproduce that sequence:
     * keep one complete canonical chain for every presentation of the intro. */
    if(enabled && tracker->levelID==NAUGHTY_DOG_CRATE) {
        sceneCanonicalReasons=NATIVE_SCENE_CANONICAL_SCRIPTED_INTRO;
        enabled=0;
    }
    /* These modes reuse world models in UI/custom push buffers. Keep complete
     * sources, but publish only views with an actual world-chain occurrence. */
    sceneWorldMembership=tracker->numPlyrCurrGame>1 || (tracker->gameMode1&(MAIN_MENU|GAME_CUTSCENE|ADVENTURE_ARENA));
    if(!sceneGame && enabled) sceneGame=NativeSceneSnapshot_Create();
    if(!sceneGame) return;
    NativeSceneSnapshot_Begin(sceneGame,enabled,signature);if(!enabled) return;sceneCapture=1;
    for(int i=0;i<tracker->numPlyrCurrGame && i<4;i++) {
        const struct PushBuffer *pb=&tracker->pushBuffer[i];
        struct NativeSceneCameraSnapshot camera={.width=(u16)pb->rect.w,.height=(u16)pb->rect.h,.h=pb->distanceToScreen_PREV,
            .nearDepth=pb->distanceToScreen_PREV/4+1,.farDepth=65535,.offset={pb->rect.w/2,pb->rect.h/2}};
        for(unsigned row=0;row<3;row++) {
            camera.view.position[row]=pb->matrix_Camera.t[row];
            for(unsigned col=0;col<3;col++) camera.view.basis[row][col]=pb->matrix_ViewProj.m[row][col]/4096.0f;
        }
        NativeSceneSnapshot_Camera(sceneGame,(unsigned)i,&camera);
    }
    if(tracker->renderFlags&RENDER_FLAG_RENDER_BUCKET) {
        if(tracker->level1 && tracker->level1->ptrInstDefs) {
            if(tracker->level1->numInstances>4096) NativeSceneSnapshot_Invalidate(sceneGame);
            else for(u32 i=0;i<tracker->level1->numInstances;i++) SceneGameCaptureInstance(tracker->level1->ptrInstDefs[i].ptrInstance);
        }
        unsigned count=0;
        for(const struct Instance *inst=(const struct Instance *)tracker->JitPools.instance.taken.first;inst;inst=inst->next) {
            if(++count>4096) {NativeSceneSnapshot_Invalidate(sceneGame);break;}
            SceneGameCaptureInstance(inst);
        }
    }
    if(tracker->renderFlags&RENDER_FLAG_DRAW_LEVEL) SceneGameCaptureTerrain(tracker->level1,(u32)tracker->timer);
#ifdef CTR_INTERNAL
    static uint64_t coverageSignature;
    if(coverageSignature!=signature && NativePerf_IsEnabled() && LOAD_IsOpen_RacingOrBattle() &&
        !(tracker->gameMode1&(LOADING|GAME_CUTSCENE|MAIN_MENU|ADVENTURE_ARENA))) {
        unsigned total=0,covered=0;
        for(unsigned i=0;i<8;i++) if(tracker->drivers[i] && tracker->drivers[i]->instSelf) {
            const struct Instance *kart=tracker->drivers[i]->instSelf;total++;
            int supports=NativeSceneSnapshot_CoversObject(sceneGame,NativeVisual_ObjectID(kart));covered+=supports;
            if(!supports) Platform_Log("[CTR Scene Snapshot] kart %u retained canonical flags=%08x\n",i,kart->flags);
        }
        Platform_Log("[CTR Scene Snapshot] kart coverage %u/%u\n",covered,total);coverageSignature=signature;
    }
#endif

}
/* Import verified special primitives after preparation has populated their
 * exact fields. Only copied values enter history; this traversal is current OT. */
static int SceneGameCaptureEffects(void)
{
    unsigned captured[13]={0},retained[13]={0};
    struct NativeSceneEffect candidate;
    for(unsigned i=0;NativeVisual_StagedEffect(i,&candidate);i++) {
        if(!NativeSceneSnapshot_Effect(sceneGame,&candidate)) return 0;
        sceneEffects++;if(candidate.kind<13) captured[candidate.kind]++;
    }
    const u32 *packet=&sceneTracker->pushBuffer[0].ptrOT[1023];
    unsigned bucket=1023,view=0,walk=0;u16 page=0;
    struct NativeSceneLayout layout=SceneGameLayout(sceneTracker);
    for(;;) {
        if(++walk>131072 || !NativeGpuLinks_IsRegisteredHostRange(packet,4)) return 0;
        NativeSceneLayout_Locate(&layout,packet,&view,&bucket);
        int inWorld=view<4;
        u32 tag=*packet;unsigned length=tag>>24;
        if(length) {
            if(length>32 || !NativeGpuLinks_IsRegisteredHostRange(packet,(length+1)*4)) return 0;
            if((packet[1]>>24)==0xe1) page=(u16)packet[1];
            if(inWorld) {
                unsigned code=packet[1]>>24;
                if(code>=0xe3 && code<=0xe5 && !SceneGameWorldEnvironment(sceneTracker,view,packet,length)) {
                    sceneCanonicalReasons|=NATIVE_SCENE_CANONICAL_OFFSCREEN;
                    return 0;
                }
            }
            unsigned kind=0;uint64_t owner=0;
            int known=NativeVisual_PacketIdentity(packet,&kind,&owner);
            if(inWorld && known && kind==1) NativeSceneSnapshot_ActivateObjectView(sceneGame,owner,view);
            int special=inWorld && known && (kind==2 || (kind>=5 && kind<=10) || kind==12 || (kind==1 && !NativeSceneSnapshot_CoversObject(sceneGame,owner) && NativeVisual_IsEffectPacket(packet)));
            if(special && !NativeVisual_EffectCovered(packet)) {
                struct NativeSceneEffect effect;
                if(NativeVisual_PacketEffect(packet,&effect)) {
                    effect.bucket=(int)bucket;
                    if(!(effect.code&4) && (packet[1]>>24)!=0xe1) effect.page=page;
                    if(!(effect.flags&NATIVE_EFFECT_STAGED)) {
                        if(!NativeSceneSnapshot_Effect(sceneGame,&effect)) return 0;
                        sceneEffects++;if(kind<13) captured[kind]++;
                        NativeVisual_MarkEffect(packet);
                    } else {
                        /* A staged source would already mark its live packet
                         * covered. Failed staging must retain the primitive. */
                        sceneEffectFallback++;if(kind<13) retained[kind]++;
                    }
                } else {
                    /* Keep the entire frame at the canonical pose if even one
                     * solid wheel lacks 3D provenance. Mixing its current-tick
                     * packet with a sampled kart body visibly detaches it. */
                    if(kind==5) return 0;
                    sceneEffectFallback++;if(kind<13) retained[kind]++;
                }
            }
            if(inWorld && known && kind==1 && !NativeSceneSnapshot_CoversObject(sceneGame,owner) && !NativeVisual_EffectCovered(packet)) sceneRetainedModels++;
        }
        if(NativeGpuLinks_IsTerminator(tag)) break;
        packet=NativeGpuLinks_ToHostPointer(tag&0xffffffu);if(!packet) return 0;
    }
#ifdef CTR_INTERNAL
    static unsigned reports;
    if(NativePerf_IsEnabled() && (++reports<=3 || (reports<=1800 && reports%300==0))) Platform_Log("[CTR Effects] captured/retained: tires %u/%u reflection %u/%u shadow %u/%u particles %u/%u sky %u/%u weather %u/%u confetti %u/%u models %u/%u\n",
        captured[5],retained[5],captured[6],retained[6],captured[7],retained[7],captured[2],retained[2],captured[10],retained[10],captured[8]+captured[12],retained[8]+retained[12],captured[9],retained[9],captured[1],retained[1]);
#endif
    return 1;
}
void NativeSceneGameSnapshot_Seal(void)
{
    if(!sceneCapture) return;
    struct NativeVisualScenePacket packet;
    for(unsigned i=0;NativeVisual_ScenePacket(i,&packet);i++) if(packet.kind==1 && packet.count==3 && packet.ordinal==1 && packet.contextCount==1 && packet.projectedMask==7) {
        struct NativeModelTriangle material={.textured=(packet.code&0xfcu)==0x34,
            .textureBlend=(packet.code&2)?NATIVE_TEXTURE_BLEND_SEMI:NATIVE_TEXTURE_BLEND_OPAQUE,
            .texture={.clut=packet.clut,.tpage=packet.page}};
        for(unsigned k=0;k<3;k++) {material.colors[k]=(packet.code&5)==5 ? 0x808080:packet.colors[k];material.texture.u[k]=packet.u[k];material.texture.v[k]=packet.v[k];}
        /* RenderBucket includes cameraID in the low part bits. Strip only
         * the viewport whose material is being installed. */
        unsigned view=packet.viewport;
        NativeSceneSnapshot_MaterialForView(sceneGame,view,packet.object,packet.primitive,packet.part^view,packet.local,&material);
    }
    if(NativeVisual_GetStats().overflow || !SceneGameCaptureEffects()) NativeSceneSnapshot_Invalidate(sceneGame);
    if(sceneRetainedModels) sceneCanonicalReasons|=NATIVE_SCENE_CANONICAL_MODEL;
    if(sceneEffectFallback) sceneCanonicalReasons|=NATIVE_SCENE_CANONICAL_EFFECT;
    /* The screen-space glow is authored from current pitch/roll. Until it has
     * a sampled source, keep its horizon and all world geometry on that pose. */
    if(sceneTerrain && sceneTracker->level1 && (sceneTracker->level1->configFlags&1)) sceneCanonicalReasons|=NATIVE_SCENE_CANONICAL_GLOW;
    if(sceneCanonicalReasons) NativeSceneSnapshot_UseCurrentPose(sceneGame);
    sceneReady=NativeSceneSnapshot_Seal(sceneGame);sceneCapture=0;
    if(!sceneReady) SceneGameCaptureError("frame",0);
}
static int SceneGameAppend(unsigned view,unsigned bucket,const u32 *words,unsigned length)
{
    if(scenePacketCount==SCENE_GAME_PACKETS || length>32 || view>4 || bucket>1023) return 0;
    struct SceneGamePacket *p=&scenePackets[scenePacketCount];memset(p,0,sizeof(*p));
    memcpy(p->words,words,(length+1)*sizeof(u32));p->words[0]=length<<24;
    unsigned record=++scenePacketCount;
    if(sceneTails[view][bucket]) scenePackets[sceneTails[view][bucket]-1].next=record;else sceneHeads[view][bucket]=record;
    sceneTails[view][bucket]=record;return 1;
}
static enum NativeAssetResult SceneGameEmitPackets(void *user,unsigned bucket,const struct NativeDrawTriangle *triangle)
{
    unsigned view=*(const unsigned *)user;u32 words[10]={0};
    if(sceneVertexBudget>MAX_VERTEX_BUFFER_SIZE-3) return NATIVE_ASSET_OUTPUT_TOO_SMALL;sceneVertexBudget+=3;
    unsigned textured=triangle->source.textured;
    if(triangle->line || (!textured && triangle->source.textureBlend==NATIVE_TEXTURE_BLEND_SEMI)) {
        u32 mode[2]={0,0xe1000000u|(triangle->source.texture.tpage&0x7ffu)};
        if(!SceneGameAppend(view,bucket,mode,1)) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    }
    if(triangle->line) {
        u32 line[5]={0,triangle->source.colors[0]|((triangle->source.textureBlend==NATIVE_TEXTURE_BLEND_SEMI?0x52u:0x50u)<<24),
            (u16)triangle->screen[0][0]|((u32)(u16)triangle->screen[0][1]<<16),triangle->source.colors[1],
            (u16)triangle->screen[1][0]|((u32)(u16)triangle->screen[1][1]<<16)};
        unsigned index=scenePacketCount;
        if(!SceneGameAppend(view,bucket,line,4)) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
        for(unsigned k=0;k<2;k++) if(!NativeVisual_PresentationXY(&scenePackets[index].words[2+k*2],triangle->visualXY[k][0],triangle->visualXY[k][1])) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
        return NATIVE_ASSET_OK;
    }
    u32 code=NativeMaterial_TriangleCode(textured,triangle->source.texture.tpage,triangle->source.textureBlend);
    for(unsigned k=0;k<3;k++) {
        unsigned color=1+k*(textured?3:2);words[color]=(triangle->source.colors[k]&0xffffffu)|(k==0 ? code<<24 : 0);
        words[color+1]=(u16)triangle->screen[k][0]|((u32)(u16)triangle->screen[k][1]<<16);
        if(textured) words[color+2]=triangle->source.texture.u[k]|((u32)triangle->source.texture.v[k]<<8)|
            (k==0 ? (u32)triangle->source.texture.clut<<16 : k==1 ? (u32)triangle->source.texture.tpage<<16 : 0);
    }
    unsigned index=scenePacketCount;
    if(!SceneGameAppend(view,bucket,words,textured?9:6)) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    if(triangle->hasVisualXY) for(unsigned k=0;k<3;k++) {
        unsigned xy=2+k*(textured?3:2);
        if(!NativeVisual_PresentationXY(&scenePackets[index].words[xy],triangle->visualXY[k][0],triangle->visualXY[k][1])) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    }
    return NATIVE_ASSET_OK;
}
/* Retail instances use nested OTs with finer depth than the outer world OT.
 * Keep generated primitive groups (including their draw mode) together and
 * order them by eye depth inside each outer bucket, so front wheels stay
 * behind the kart body even when both round to the same world bucket. */
static enum NativeAssetResult SceneGameEmit(void *user,unsigned bucket,const struct NativeDrawTriangle *triangle)
{
    unsigned view=*(const unsigned *)user,before=scenePacketCount;
    unsigned head=sceneHeads[view][bucket],tail=sceneTails[view][bucket];
    enum NativeAssetResult result=SceneGameEmitPackets(user,bucket,triangle);
    if(result!=NATIVE_ASSET_OK) return result;
    sceneHeads[view][bucket]=head;sceneTails[view][bucket]=tail;
    if(tail) scenePackets[tail-1].next=0;
    if(sceneGroupCount==SCENE_GAME_PACKETS) return NATIVE_ASSET_OUTPUT_TOO_SMALL;
    unsigned depth=0;for(unsigned k=0;k<(triangle->line?2u:3u);k++) if(triangle->depth[k]>depth) depth=triangle->depth[k];
    sceneGroups[sceneGroupCount]=(struct SceneGameGroup){before+1,scenePacketCount,view,bucket,depth,sceneGroupCount};sceneGroupCount++;
    return NATIVE_ASSET_OK;
}
static int SceneGameGroupCompare(const void *left,const void *right)
{
    const struct SceneGameGroup *a=left,*b=right;
    if(a->view!=b->view) return a->view<b->view ? -1:1;
    if(a->bucket!=b->bucket) return a->bucket>b->bucket ? -1:1;
    if(a->depth!=b->depth) return a->depth>b->depth ? -1:1;
    return a->order<b->order ? -1:a->order>b->order;
}
uint32_t *NativeSceneGameSnapshot_Presentation(struct GameTracker *tracker,float alpha)
{
    sceneStats=(struct NativeSceneGameSnapshotStats){.effects=sceneEffects,.effectFallback=sceneEffectFallback,
        .retainedModelPackets=sceneRetainedModels,.canonicalPoseReasons=sceneCanonicalReasons};
    u32 *canonical=&tracker->pushBuffer[0].ptrOT[1023];
    if(!sceneReady || !sceneGame || sceneEpoch!=NativeVisual_ResetEpoch()) {sceneStats.fallback=sceneWanted;return canonical;}
    scenePacketCount=sceneVertexBudget=sceneGroupCount=0;memset(sceneHeads,0,sizeof(sceneHeads));memset(sceneTails,0,sizeof(sceneTails));
    struct NativeSceneLayout layout=SceneGameLayout(tracker);
    unsigned view=0,bucket=1023,walk=0;const u32 *packet=canonical;
    for(;;) {
        if(++walk>131072 || !NativeGpuLinks_IsRegisteredHostRange(packet,4)) goto fallback;
        NativeSceneLayout_Locate(&layout,packet,&view,&bucket);
        u32 tag=packet[0];unsigned length=tag>>24,kind=0;uint64_t owner=0;
        if(length) {
            if(length>32 || !NativeGpuLinks_IsRegisteredHostRange(packet,(length+1)*4)) goto fallback;
            int known=NativeVisual_PacketIdentity(packet,&kind,&owner);
            int replace=view<4 && known && ((kind==3 && sceneTerrain) || (kind==1 && NativeSceneSnapshot_CoversObject(sceneGame,owner)) || NativeVisual_EffectCovered(packet));
            if(!replace) {
                if(!SceneGameAppend(view,bucket,packet,length)) goto fallback;
                /* Conservatively bound every retained GPU word as a vertex. */
                sceneVertexBudget+=length*2;if(sceneVertexBudget>MAX_VERTEX_BUFFER_SIZE) goto fallback;
            }
        }
        if(NativeGpuLinks_IsTerminator(tag)) break;
        packet=NativeGpuLinks_ToHostPointer(tag&0xffffffu);if(!packet) goto fallback;
    }
    for(unsigned i=0;i<(unsigned)tracker->numPlyrCurrGame;i++) {
        struct NativeSceneSnapshotStats stats;
        if(NativeSceneSnapshot_Render(sceneGame,i,alpha,SceneGameEmit,&i,&stats)!=NATIVE_ASSET_OK) goto fallback;
        sceneStats.objects=stats.objects;sceneStats.sources=stats.sources;sceneStats.triangles+=stats.triangles;
        sceneStats.matched=stats.matched;sceneStats.unsupported=stats.unsupported;
        sceneStats.currentPose=stats.currentPose;
    }
    qsort(sceneGroups,sceneGroupCount,sizeof(*sceneGroups),SceneGameGroupCompare);
    for(unsigned i=0;i<sceneGroupCount;i++) {
        const struct SceneGameGroup *group=&sceneGroups[i];unsigned *tail=&sceneTails[group->view][group->bucket];
        if(*tail) scenePackets[*tail-1].next=group->head;else sceneHeads[group->view][group->bucket]=group->head;
        *tail=group->tail;
    }
    if(!NativeGpuLinks_IsRegisteredHostRange(scenePackets,sizeof(scenePackets)) && !NativeGpuLinks_RegisterRange(scenePackets,sizeof(scenePackets),NULL)) goto fallback;
    unsigned first=0,last=0;
    for(unsigned i=0;i<5;i++) for(int depth=1023;depth>=0;depth--) {
        unsigned head=sceneHeads[i][depth];if(!head) continue;
        if(last) scenePackets[last-1].next=head;else first=head;last=sceneTails[i][depth];
    }
    if(!first) goto fallback;
    for(unsigned i=0;i<scenePacketCount;i++) {
        unsigned next=scenePackets[i].next;
        u32 token=next ? NativeGpuLinks_FromHostPointer(scenePackets[next-1].words) : NATIVE_GPU_LINK_TERMINATOR;
        if(next && token==NATIVE_GPU_LINK_TERMINATOR) goto fallback;
        scenePackets[i].words[0]|=token;
    }
    sceneStats.regenerated=1;return scenePackets[first-1].words;
fallback:
    /* Do not alternate canonical/current and delayed sampled poses within one
     * prepared tick after a capacity/packet failure. Restart history next tick. */
    sceneReady=0;NativeSceneSnapshot_Reset(sceneGame);
    sceneStats.fallback=1;return canonical;
}
struct NativeSceneGameSnapshotStats NativeSceneGameSnapshot_GetStats(void) {return sceneStats;}
