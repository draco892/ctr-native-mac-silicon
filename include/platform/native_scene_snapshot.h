#ifndef NATIVE_SCENE_SNAPSHOT_H
#define NATIVE_SCENE_SNAPSHOT_H
#include <platform/native_scene_geometry.h>
#include <stdint.h>

/* Value-only scene history. Asset views, game pointers and GPU arenas are never
 * retained. Inputs are copied before projection, visibility tests or clipping. */
struct NativeScenePose { float basis[3][3], position[3]; };
struct NativeSceneObjectSnapshot {
    uint64_t id, modelIdentity;
    struct NativeScenePose pose;
    uint32_t flags, animation, frame, header;
    int supported;
};
struct NativeSceneCameraSnapshot {
    struct NativeScenePose view;
    unsigned width,height;
    float h,offset[2];
    int nearDepth,farDepth;
};
struct NativeSceneSourceTriangle {
    uint64_t object,primitive;
    struct NativeModelTriangle material;
    float local[3][3];
    int cull; /* 0=double-sided, +1=positive winding, -1=negative. */
    int reflectedCull; /* Terrain winding is authored before the view reflection. */
    s16 orderingBias;
};
struct NativeSceneSnapshot;
struct NativeSceneSnapshotStats { unsigned objects,sources,triangles,culled,clipped,matched,unsupported; int valid; };
typedef enum NativeAssetResult (*NativeSceneSnapshotSink)(void *,unsigned bucket,const struct NativeDrawTriangle *);
struct NativeSceneSnapshot *NativeSceneSnapshot_Create(void);
void NativeSceneSnapshot_Destroy(struct NativeSceneSnapshot *);
void NativeSceneSnapshot_Reset(struct NativeSceneSnapshot *);
void NativeSceneSnapshot_Begin(struct NativeSceneSnapshot *,int enabled,uint64_t signature);
int NativeSceneSnapshot_Camera(struct NativeSceneSnapshot *,unsigned,const struct NativeSceneCameraSnapshot *);
int NativeSceneSnapshot_Object(struct NativeSceneSnapshot *,const struct NativeSceneObjectSnapshot *);
int NativeSceneSnapshot_Triangle(struct NativeSceneSnapshot *,const struct NativeSceneSourceTriangle *);
int NativeSceneSnapshot_Material(struct NativeSceneSnapshot *,uint64_t object,uint64_t primitive,uint64_t modelIdentity,const float local[3][3],const struct NativeModelTriangle *);
void NativeSceneSnapshot_Invalidate(struct NativeSceneSnapshot *);
int NativeSceneSnapshot_Seal(struct NativeSceneSnapshot *);
int NativeSceneSnapshot_CoversObject(const struct NativeSceneSnapshot *,uint64_t);
int NativeSceneSnapshot_CameraPose(const struct NativeSceneSnapshot *,unsigned,float,struct NativeSceneCameraSnapshot *);
enum NativeAssetResult NativeSceneSnapshot_Render(const struct NativeSceneSnapshot *,unsigned,float,
    NativeSceneSnapshotSink,void *,struct NativeSceneSnapshotStats *);
#endif
