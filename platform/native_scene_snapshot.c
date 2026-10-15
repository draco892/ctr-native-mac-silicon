#include <platform/native_scene_snapshot.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SNAP_OBJECTS 2048u
#define SNAP_SOURCES 65536u
#define SNAP_OBJECT_TABLE 4096u
#define SNAP_SOURCE_TABLE 131072u
struct SnapshotFrame {
    unsigned objectCount,sourceCount,cameraMask;
    struct NativeSceneCameraSnapshot cameras[4];
    struct NativeSceneObjectSnapshot objects[SNAP_OBJECTS];
    struct NativeSceneSourceTriangle sources[SNAP_SOURCES];
    unsigned objectMap[SNAP_OBJECT_TABLE],sourceMap[SNAP_SOURCE_TABLE];
    unsigned materialCount,materialMap[SNAP_SOURCES][4];
    struct NativeModelTriangle materials[SNAP_SOURCES];
    int valid,currentPose;
};
struct NativeSceneSnapshot { struct SnapshotFrame frames[2]; unsigned current; uint64_t signature; int capturing,history; };
static uint64_t SnapshotHash(uint64_t x)
{
    x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static float SnapshotLerp(float a,float b,float t) { return a+(b-a)*t; }
static float SnapshotAlpha(float t) { return isfinite(t)?fminf(1,fmaxf(0,t)):1; }
/* Only interpolate attributes belonging to the same material. UVs for sprite
 * and atlas animation select an entire frame; moving them through the atlas
 * would sample unrelated texels. Water explicitly opts into continuous UVs. */
static struct NativeModelTriangle SnapshotMaterial(const struct NativeSceneSourceTriangle *a,
    const struct NativeSceneSourceTriangle *b,float t,struct NativeSceneClipVertex vertices[3])
{
    const struct NativeModelTriangle *current=&b->material,*previous=a ? &a->material:NULL;
    int compatible=previous && a->continuousUV==b->continuousUV && a->line==b->line &&
        previous->textured==current->textured && previous->textureBlend==current->textureBlend &&
        previous->texture.tpage==current->texture.tpage && previous->texture.clut==current->texture.clut;
    if(compatible && current->textured) for(unsigned k=0;k<3;k++) {
        int du=(int)current->texture.u[k]-previous->texture.u[k],dv=(int)current->texture.v[k]-previous->texture.v[k];
        /* Byte-coordinate wrap is a discontinuity. Never sweep through the
         * middle of the page or interpolate a different atlas frame. */
        if(b->continuousUV ? (abs(du)>128 || abs(dv)>128):(du!=0 || dv!=0)) compatible=0;
    }
    struct NativeModelTriangle material=previous && !compatible && t<1 ? *previous:*current;
    for(unsigned k=0;k<3;k++) {
        NativeSceneGeometry_Attributes(&material,k,&vertices[k]);
        if(!compatible) continue;
        for(unsigned channel=0;channel<3;channel++) {
            unsigned x=(previous->colors[k]>>(channel*8))&255,y=(current->colors[k]>>(channel*8))&255;
            vertices[k].rgb[channel]=(s32)lroundf(SnapshotLerp((float)x,(float)y,t)*65536);
        }
        if(b->continuousUV) {
            vertices[k].uv[0]=(s32)lroundf(SnapshotLerp(previous->texture.u[k],current->texture.u[k],t)*65536);
            vertices[k].uv[1]=(s32)lroundf(SnapshotLerp(previous->texture.v[k],current->texture.v[k],t)*65536);
        }
    }
    return material;
}
static const struct NativeSceneObjectSnapshot *SnapshotObject(const struct SnapshotFrame *f,uint64_t id)
{
    unsigned slot=(unsigned)SnapshotHash(id)&(SNAP_OBJECT_TABLE-1);
    while(f->objectMap[slot]) {
        const struct NativeSceneObjectSnapshot *o=&f->objects[f->objectMap[slot]-1];
        if(o->id==id) return o;
        slot=(slot+1)&(SNAP_OBJECT_TABLE-1);
    }
    return NULL;
}
static const struct NativeSceneSourceTriangle *SnapshotSource(const struct SnapshotFrame *f,uint64_t object,uint64_t primitive)
{
    unsigned slot=(unsigned)SnapshotHash(object^SnapshotHash(primitive))&(SNAP_SOURCE_TABLE-1);
    while(f->sourceMap[slot]) {
        const struct NativeSceneSourceTriangle *s=&f->sources[f->sourceMap[slot]-1];
        if(s->object==object && s->primitive==primitive) return s;
        slot=(slot+1)&(SNAP_SOURCE_TABLE-1);
    }
    return NULL;
}
struct NativeSceneSnapshot *NativeSceneSnapshot_Create(void) { return calloc(1,sizeof(struct NativeSceneSnapshot)); }
void NativeSceneSnapshot_Destroy(struct NativeSceneSnapshot *s) { free(s); }
void NativeSceneSnapshot_Reset(struct NativeSceneSnapshot *s)
{
    if(!s) return;
    s->capturing=s->history=0;s->frames[0].valid=s->frames[1].valid=0;
}
void NativeSceneSnapshot_Begin(struct NativeSceneSnapshot *s,int enabled,uint64_t signature)
{
    if(!s) return;
    if(!enabled) {NativeSceneSnapshot_Reset(s);return;}
    if(s->signature!=signature) NativeSceneSnapshot_Reset(s);
    s->signature=signature;s->history=s->frames[s->current].valid;
    s->current^=1;
    struct SnapshotFrame *f=&s->frames[s->current];
    f->objectCount=f->sourceCount=f->cameraMask=f->materialCount=0;f->valid=1;f->currentPose=0;
    memset(f->objectMap,0,sizeof(f->objectMap));memset(f->sourceMap,0,sizeof(f->sourceMap));
    s->capturing=1;
}
void NativeSceneSnapshot_Invalidate(struct NativeSceneSnapshot *s) { if(s) s->frames[s->current].valid=0; }
void NativeSceneSnapshot_UseCurrentPose(struct NativeSceneSnapshot *s)
{ if(s && s->capturing) s->frames[s->current].currentPose=1; }
static int SnapshotPoseValid(const struct NativeScenePose *p)
{
    for(unsigned row=0;row<3;row++) {
        if(!isfinite(p->position[row]) || fabsf(p->position[row])>2147483648.0f) return 0;
        for(unsigned col=0;col<3;col++) if(!isfinite(p->basis[row][col]) || fabsf(p->basis[row][col])>64) return 0;
    }
    return 1;
}
int NativeSceneSnapshot_Camera(struct NativeSceneSnapshot *s,unsigned view,const struct NativeSceneCameraSnapshot *c)
{
    if(!s || !s->capturing || !c || view>=4) return 0;
    if(!SnapshotPoseValid(&c->view) || !c->width || !c->height || c->width>4096 || c->height>4096 ||
        !isfinite(c->h) || c->h<=0 || c->h>65535 || c->nearDepth<1 || c->farDepth>65535 || c->nearDepth>=c->farDepth ||
        !isfinite(c->offset[0]) || !isfinite(c->offset[1]) || fabsf(c->offset[0])>32767 || fabsf(c->offset[1])>32767) {NativeSceneSnapshot_Invalidate(s);return 0;}
    struct SnapshotFrame *f=&s->frames[s->current];f->cameras[view]=*c;f->cameraMask|=1u<<view;return 1;
}
int NativeSceneSnapshot_Object(struct NativeSceneSnapshot *s,const struct NativeSceneObjectSnapshot *o)
{
    if(!s || !s->capturing || !o) return 0;
    struct SnapshotFrame *f=&s->frames[s->current];
    if(!f->valid || !SnapshotPoseValid(&o->pose) || f->objectCount==SNAP_OBJECTS) {NativeSceneSnapshot_Invalidate(s);return 0;}
    unsigned slot=(unsigned)SnapshotHash(o->id)&(SNAP_OBJECT_TABLE-1);
    while(f->objectMap[slot]) {
        if(f->objects[f->objectMap[slot]-1].id==o->id) return 0; /* Duplicate traversal is harmless. */
        slot=(slot+1)&(SNAP_OBJECT_TABLE-1);
    }
    f->objects[f->objectCount]=*o;f->objectMap[slot]=++f->objectCount;return 1;
}
void NativeSceneSnapshot_ActivateObjectView(struct NativeSceneSnapshot *s,uint64_t id,unsigned view)
{
    if(!s || !s->capturing || view>=4) return;
    struct SnapshotFrame *f=&s->frames[s->current];
    const struct NativeSceneObjectSnapshot *object=SnapshotObject(f,id);
    if(object && (object->viewportMask&NATIVE_SCENE_WORLD_MEMBERSHIP)) f->objects[object-f->objects].viewportMask|=1u<<view;
}
int NativeSceneSnapshot_Triangle(struct NativeSceneSnapshot *s,const struct NativeSceneSourceTriangle *t)
{
    if(!s || !s->capturing || !t) return 0;
    struct SnapshotFrame *f=&s->frames[s->current];
    if(!f->valid || f->sourceCount==SNAP_SOURCES || !SnapshotObject(f,t->object) || t->cull<-1 || t->cull>1) {NativeSceneSnapshot_Invalidate(s);return 0;}
    for(unsigned k=0;k<3;k++) for(unsigned axis=0;axis<3;axis++)
        if(!isfinite(t->local[k][axis]) || fabsf(t->local[k][axis])>(1<<24)) {NativeSceneSnapshot_Invalidate(s);return 0;}
    unsigned slot=(unsigned)SnapshotHash(t->object^SnapshotHash(t->primitive))&(SNAP_SOURCE_TABLE-1);
    while(f->sourceMap[slot]) {
        const struct NativeSceneSourceTriangle *existing=&f->sources[f->sourceMap[slot]-1];
        if(existing->object==t->object && existing->primitive==t->primitive) {NativeSceneSnapshot_Invalidate(s);return 0;}
        slot=(slot+1)&(SNAP_SOURCE_TABLE-1);
    }
    memset(f->materialMap[f->sourceCount],0,sizeof(f->materialMap[0]));
    f->sources[f->sourceCount]=*t;f->sourceMap[slot]=++f->sourceCount;return 1;
}
int NativeSceneSnapshot_MaterialForView(struct NativeSceneSnapshot *s,unsigned view,uint64_t object,uint64_t primitive,uint64_t identity,const float local[3][3],const struct NativeModelTriangle *material)
{
    if(!s || !s->capturing || !material || !local || view>=4) return 0;
    struct SnapshotFrame *f=&s->frames[s->current];
    const struct NativeSceneObjectSnapshot *o=SnapshotObject(f,object);
    if(!o || o->modelIdentity!=identity) return 0;
    const struct NativeSceneSourceTriangle *source=SnapshotSource(f,object,primitive);
    if(!source) return 0;
    /* A clipped, split or reordered GPU face cannot supply corner attributes
     * for the uncut source triangle. Keep its authored material instead. */
    for(unsigned k=0;k<3;k++) for(unsigned axis=0;axis<3;axis++)
        if(!isfinite(local[k][axis]) || fabsf(local[k][axis]-source->local[k][axis])>0.1f) return 0;
    struct NativeSceneSourceTriangle *destination=&f->sources[source-f->sources];
    struct NativeModelTriangle updated=*material;
    memcpy(updated.vertices,destination->material.vertices,sizeof(updated.vertices));updated.command=destination->material.command;
    unsigned *entry=&f->materialMap[destination-f->sources][view];
    if(!*entry) {
        if(f->materialCount==SNAP_SOURCES) {NativeSceneSnapshot_Invalidate(s);return 0;}
        *entry=++f->materialCount;
    }
    f->materials[*entry-1]=updated;return 1;
}
int NativeSceneSnapshot_Material(struct NativeSceneSnapshot *s,uint64_t object,uint64_t primitive,uint64_t identity,const float local[3][3],const struct NativeModelTriangle *material)
{ return NativeSceneSnapshot_MaterialForView(s,0,object,primitive,identity,local,material); }
static struct NativeSceneSourceTriangle SnapshotSourceForView(const struct SnapshotFrame *f,const struct NativeSceneSourceTriangle *source,unsigned view)
{
    struct NativeSceneSourceTriangle result=*source;
    unsigned entry=f->materialMap[source-f->sources][view];
    if(entry) result.material=f->materials[entry-1];
    return result;
}
static int SnapshotInverse(const float matrix[3][3],const float input[3],float out[3])
{
    float det=matrix[0][0]*(matrix[1][1]*matrix[2][2]-matrix[1][2]*matrix[2][1])-
        matrix[0][1]*(matrix[1][0]*matrix[2][2]-matrix[1][2]*matrix[2][0])+
        matrix[0][2]*(matrix[1][0]*matrix[2][1]-matrix[1][1]*matrix[2][0]);
    if(!isfinite(det) || fabsf(det)<0.000001f) return 0;
    for(unsigned row=0;row<3;row++) {
        out[row]=0;
        for(unsigned col=0;col<3;col++) {
            unsigned a=(col+1)%3,b=(col+2)%3,c=(row+1)%3,d=(row+2)%3;
            out[row]+=(matrix[a][c]*matrix[b][d]-matrix[a][d]*matrix[b][c])*input[col]/det;
        }
        if(!isfinite(out[row]) || fabsf(out[row])>(1<<24)) return 0;
    }
    return 1;
}
int NativeSceneSnapshot_Effect(struct NativeSceneSnapshot *s,const struct NativeSceneEffect *effect)
{
    if(!s || !s->capturing || !effect || effect->viewport>=4 || effect->count<2 || effect->count>4 || effect->bucket<0 || effect->bucket>1023) return 0;
    struct SnapshotFrame *f=&s->frames[s->current];
    if(!f->valid || !(f->cameraMask&(1u<<effect->viewport))) return 0;
    const struct NativeSceneCameraSnapshot *camera=&f->cameras[effect->viewport];
    const struct NativeSceneObjectSnapshot *anchor=effect->anchor ? SnapshotObject(f,effect->anchor):NULL;
    if(effect->anchor && !anchor) return 0;
    int wheel=effect->kind==5 || effect->kind==6;
    if(wheel && (effect->count!=4 || effect->topology>=8 || !anchor)) return 0;
    /* An interpolated wheel cannot attach to a retained current-tick body. */
    if(effect->kind==5 && !anchor->supported) return 0;
    unsigned packetCorner[4]={0,1,2,3};
    if(wheel) for(unsigned k=0;k<4;k++) packetCorner[NativeSceneEffect_WheelCorner(effect->topology,k)]=k;
    struct NativeSceneObjectSnapshot object={.id=effect->id,.modelIdentity=effect->flags^((uint64_t)effect->topology<<32),.supported=1,.viewportMask=1u<<effect->viewport,.renderMode=effect->flags,
        .pose={.basis={{1,0,0},{0,1,0},{0,0,1}}}};
    float world[4][3]={{0}},local[4][3]={{0}},centre[3]={0},maxDepth=0;
    for(unsigned k=0;k<effect->count;k++) {
        for(unsigned axis=0;axis<3;axis++) if(!isfinite(effect->eye[k][axis]) || fabsf(effect->eye[k][axis])>(1<<24)) return 0;
        if(!SnapshotInverse(camera->view.basis,effect->eye[k],world[k])) return 0;
        for(unsigned axis=0;axis<3;axis++) {
            if(!(effect->flags&NATIVE_EFFECT_DIRECTIONAL)) world[k][axis]+=camera->view.position[axis];
            centre[axis]+=effect->eye[k][axis]/effect->count;
        }
        if(effect->eye[k][2]>maxDepth) maxDepth=effect->eye[k][2];
    }
    if(anchor) {
        object.pose=anchor->pose;object.modelIdentity=anchor->modelIdentity;object.animation=anchor->animation;object.header=anchor->header;
        object.poseIdentity=anchor->poseIdentity;
        if(wheel) {object.modelIdentity=anchor->poseIdentity ? anchor->poseIdentity:anchor->modelIdentity;object.animation=UINT32_MAX;}
        for(unsigned k=0;k<effect->count;k++) {
            float relative[3];for(unsigned axis=0;axis<3;axis++) relative[axis]=world[k][axis]-object.pose.position[axis];
            if(!SnapshotInverse(object.pose.basis,relative,local[k])) return 0;
        }
    } else if(effect->flags&NATIVE_EFFECT_DIRECTIONAL) memcpy(local,world,sizeof(local));
    else {
        if(!SnapshotInverse(camera->view.basis,centre,object.pose.position)) return 0;
        for(unsigned axis=0;axis<3;axis++) object.pose.position[axis]+=camera->view.position[axis];
        for(unsigned k=0;k<effect->count;k++) for(unsigned axis=0;axis<3;axis++)
            local[k][axis]=effect->flags&NATIVE_EFFECT_BILLBOARD ? effect->eye[k][axis]-centre[axis] : world[k][axis]-object.pose.position[axis];
    }
    /* Validate before mutating the frame, so unsupported effects can retain
     * their complete canonical primitive without publishing half a quad. */
    if(!SnapshotPoseValid(&object.pose)) return 0;
    for(unsigned k=0;k<effect->count;k++) for(unsigned axis=0;axis<3;axis++)
        if(!isfinite(local[k][axis]) || fabsf(local[k][axis])>(1<<24)) return 0;
    if(!NativeSceneSnapshot_Object(s,&object)) return 0;
    const unsigned corners[2][3]={{0,1,2},{1,3,2}};
    unsigned triangles=effect->count==4 ? 2:1;
    for(unsigned part=0;part<triangles;part++) {
        struct NativeSceneSourceTriangle source={.object=object.id,.primitive=part+1,.effect=1,.line=effect->count==2,
            .fixedBucket=effect->flags&NATIVE_EFFECT_FIXED_BUCKET ? (unsigned)effect->bucket+1:0,
            .material={.textured=(effect->code&4)!=0 && effect->count!=2,
                .textureBlend=effect->code&2 ? NATIVE_TEXTURE_BLEND_SEMI:NATIVE_TEXTURE_BLEND_OPAQUE,
                .texture={.tpage=effect->page,.clut=effect->clut}}};
        int bias=effect->bucket-((int)fmaxf(0,maxDepth)>>6);if(bias<-32768) bias=-32768;if(bias>32767) bias=32767;source.orderingBias=anchor ? anchor->orderingBias:(s16)bias;
        for(unsigned k=0;k<3;k++) {
            unsigned corner=source.line ? (k?1:0):corners[part][k];
            unsigned capturedCorner=packetCorner[corner];
            memcpy(source.local[k],local[capturedCorner],sizeof(source.local[k]));source.material.vertices[k]=(wheel ? 0:effect->topology*4)+corner;
            source.material.colors[k]=(effect->code&5)==5 ? 0x808080:effect->colors[capturedCorner];source.material.texture.u[k]=effect->u[capturedCorner];source.material.texture.v[k]=effect->v[capturedCorner];
        }
        if(!NativeSceneSnapshot_Triangle(s,&source)) return 0;
    }
    return 1;
}
int NativeSceneSnapshot_Seal(struct NativeSceneSnapshot *s)
{
    if(!s || !s->capturing) return 0;
    s->capturing=0;return s->frames[s->current].valid && s->frames[s->current].cameraMask!=0;
}
int NativeSceneSnapshot_CoversObject(const struct NativeSceneSnapshot *s,uint64_t id)
{
    if(!s || !s->frames[s->current].valid) return 0;
    const struct NativeSceneObjectSnapshot *o=SnapshotObject(&s->frames[s->current],id);return o && o->supported;
}
/* Signed column scales retain reflected camera bases. Rotation interpolation
 * chooses the quaternion hemisphere once per pose, never per screen vertex. */
static int SnapshotQuaternion(const float m[3][3],float q[4],float scale[3])
{
    float r[3][3];
    float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    for(unsigned col=0;col<3;col++) {
        float length=0;for(unsigned row=0;row<3;row++) length+=m[row][col]*m[row][col];
        scale[col]=sqrtf(length);if(scale[col]<0.00001f) return 0;
        if(col==2 && det<0) scale[col]=-scale[col];
        for(unsigned row=0;row<3;row++) r[row][col]=m[row][col]/scale[col];
    }
    for(unsigned a=0;a<3;a++) for(unsigned b=a+1;b<3;b++) {
        float dot=0;for(unsigned row=0;row<3;row++) dot+=r[row][a]*r[row][b];if(fabsf(dot)>0.02f) return 0;
    }
    float trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0) {float v=sqrtf(trace+1)*2;q[3]=v/4;q[0]=(r[2][1]-r[1][2])/v;q[1]=(r[0][2]-r[2][0])/v;q[2]=(r[1][0]-r[0][1])/v;}
    else {
        unsigned a=0;if(r[1][1]>r[a][a]) a=1;if(r[2][2]>r[a][a]) a=2;unsigned b=(a+1)%3,c=(a+2)%3;
        float v=sqrtf(1+r[a][a]-r[b][b]-r[c][c])*2;q[a]=v/4;q[b]=(r[a][b]+r[b][a])/v;q[c]=(r[a][c]+r[c][a])/v;q[3]=(r[c][b]-r[b][c])/v;
    }
    return 1;
}
static struct NativeScenePose SnapshotBlendPose(const struct NativeScenePose *a,const struct NativeScenePose *b,float t)
{
    if(t>=1) return *b;
    struct NativeScenePose p=*b;float qa[4],qb[4],sa[3],sb[3];
    for(unsigned row=0;row<3;row++) p.position[row]=SnapshotLerp(a->position[row],b->position[row],t);
    if(!SnapshotQuaternion(a->basis,qa,sa) || !SnapshotQuaternion(b->basis,qb,sb)) {
        for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) p.basis[row][col]=SnapshotLerp(a->basis[row][col],b->basis[row][col],t);
        return p;
    }
    if(sa[2]*sb[2]<0) return *b; /* A handedness switch is a camera cut. */
    if(t<=0) return *a;
    float dot=0;for(unsigned i=0;i<4;i++) dot+=qa[i]*qb[i];if(dot<0) {dot=-dot;for(unsigned i=0;i<4;i++) qb[i]=-qb[i];}if(dot>1) dot=1;
    float wa=1-t,wb=t;
    if(dot<0.9995f) {float angle=acosf(dot),den=sinf(angle);wa=sinf((1-t)*angle)/den;wb=sinf(t*angle)/den;}
    float q[4],norm=0;for(unsigned i=0;i<4;i++) {q[i]=qa[i]*wa+qb[i]*wb;norm+=q[i]*q[i];}norm=sqrtf(norm);for(unsigned i=0;i<4;i++) q[i]/=norm;
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float r[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
        {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) p.basis[row][col]=r[row][col]*SnapshotLerp(sa[col],sb[col],t);
    return p;
}
static struct NativeScenePose SnapshotBlendView(const struct NativeScenePose *a,const struct NativeScenePose *b,float t)
{
    /* Model scale belongs to columns; camera aspect correction belongs to
     * rows (retail scales the Y row by 9/16). Decomposing view columns turns
     * pitch/roll into shear and changes apparent height during acceleration. */
    struct NativeScenePose left=*a,right=*b;
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) {
        left.basis[row][col]=a->basis[col][row];right.basis[row][col]=b->basis[col][row];
    }
    struct NativeScenePose result=SnapshotBlendPose(&left,&right,t),view=result;
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<3;col++) view.basis[row][col]=result.basis[col][row];
    return view;
}
int NativeSceneSnapshot_CameraPose(const struct NativeSceneSnapshot *s,unsigned view,float alpha,struct NativeSceneCameraSnapshot *out)
{
    if(!s || !out || view>=4 || !s->frames[s->current].valid || !(s->frames[s->current].cameraMask&(1u<<view))) return 0;
    const struct SnapshotFrame *f=&s->frames[s->current],*old=&s->frames[s->current^1];*out=f->cameras[view];
    if(!f->currentPose && s->history && old->valid && (old->cameraMask&(1u<<view)) && old->cameras[view].width==out->width && old->cameras[view].height==out->height) {
        out->view=SnapshotBlendView(&old->cameras[view].view,&out->view,SnapshotAlpha(alpha));
        out->h=SnapshotLerp(old->cameras[view].h,out->h,SnapshotAlpha(alpha));
        for(unsigned axis=0;axis<2;axis++) out->offset[axis]=SnapshotLerp(old->cameras[view].offset[axis],out->offset[axis],SnapshotAlpha(alpha));
    }
    return 1;
}
enum NativeAssetResult NativeSceneSnapshot_Render(const struct NativeSceneSnapshot *s,unsigned view,float alpha,
    NativeSceneSnapshotSink sink,void *user,struct NativeSceneSnapshotStats *stats)
{
    if(!stats) return NATIVE_ASSET_INVALID_ARGUMENT;memset(stats,0,sizeof(*stats));
    struct NativeSceneCameraSnapshot camera;
    if(!NativeSceneSnapshot_CameraPose(s,view,alpha,&camera)) return NATIVE_ASSET_NOT_FOUND;
    struct NativeSceneCamera clipCamera={.width=camera.width,.height=camera.height,.nearDepth=camera.nearDepth,.farDepth=camera.farDepth};
    clipCamera.transform.h=(u16)lroundf(camera.h);
    for(unsigned axis=0;axis<2;axis++) clipCamera.transform.offset[axis]=(s32)lroundf(camera.offset[axis]*65536);
    const struct SnapshotFrame *f=&s->frames[s->current],*old=&s->frames[s->current^1];float t=f->currentPose ? 1:SnapshotAlpha(alpha);
    stats->currentPose=f->currentPose;
    struct NativeScenePose poses[SNAP_OBJECTS];
    const struct NativeSceneObjectSnapshot *previousObjects[SNAP_OBJECTS]={0};
    for(unsigned i=0;i<f->objectCount;i++) {
        const struct NativeSceneObjectSnapshot *o=&f->objects[i];poses[i]=o->pose;
        const struct NativeSceneObjectSnapshot *p=s->history && old->valid ? SnapshotObject(old,o->id):NULL;
        uint64_t poseIdentity=o->poseIdentity ? o->poseIdentity:o->modelIdentity;
        if(p && p->supported==o->supported && p->renderMode==o->renderMode && p->viewportMask==o->viewportMask &&
            (p->poseIdentity ? p->poseIdentity:p->modelIdentity)==poseIdentity && p->header==o->header) {
            int continuous=1;for(unsigned axis=0;axis<3;axis++) if(fabsf(p->pose.position[axis]-o->pose.position[axis])>1024) continuous=0;
            if(continuous) {poses[i]=SnapshotBlendPose(&p->pose,&o->pose,t);previousObjects[i]=p;stats->matched++;}
        }
    }
    // Compose the sampled camera/object transform once per object, so all
    // shared vertices use exactly the same matrix and per-triangle work stays
    // bounded even when the complete source scene is much larger than the view.
    for(unsigned i=0;i<f->objectCount;i++) {
        struct NativeScenePose world=poses[i];
        for(unsigned row=0;row<3;row++) {
            poses[i].position[row]=0;
            for(unsigned axis=0;axis<3;axis++) poses[i].position[row]+=camera.view.basis[row][axis]*(world.position[axis]-((f->objects[i].renderMode&NATIVE_EFFECT_DIRECTIONAL)?0:camera.view.position[axis]));
            for(unsigned col=0;col<3;col++) {
                poses[i].basis[row][col]=0;
                for(unsigned axis=0;axis<3;axis++) poses[i].basis[row][col]+=camera.view.basis[row][axis]*world.basis[axis][col];
                if(f->objects[i].renderMode&NATIVE_EFFECT_BILLBOARD) poses[i].basis[row][col]=row==col ? 1:0;
            }
        }
    }
    const float (*viewBasis)[3]=camera.view.basis;
    float viewDeterminant=viewBasis[0][0]*(viewBasis[1][1]*viewBasis[2][2]-viewBasis[1][2]*viewBasis[2][1])-
        viewBasis[0][1]*(viewBasis[1][0]*viewBasis[2][2]-viewBasis[1][2]*viewBasis[2][0])+
        viewBasis[0][2]*(viewBasis[1][0]*viewBasis[2][1]-viewBasis[1][1]*viewBasis[2][0]);
    stats->objects=f->objectCount;stats->sources=f->sourceCount;
    for(unsigned i=0;i<f->objectCount;i++) if(!f->objects[i].supported) stats->unsupported++;
    for(unsigned i=0;i<f->sourceCount;i++) {
        const struct NativeSceneSourceTriangle *source=&f->sources[i];
        const struct NativeSceneObjectSnapshot *object=SnapshotObject(f,source->object);
        const struct NativeSceneSourceTriangle *previousSource=NULL;
        if(!object || !object->supported || (object->viewportMask && !(object->viewportMask&(1u<<view)))) continue;
        unsigned objectIndex=(unsigned)(object-f->objects);
        struct NativeScenePose pose=poses[objectIndex];
        const struct NativeSceneObjectSnapshot *previousObject=previousObjects[objectIndex];
        if(previousObject && previousObject->modelIdentity==object->modelIdentity && previousObject->animation==object->animation) {
            previousSource=SnapshotSource(old,source->object,source->primitive);
            if(previousSource && memcmp(previousSource->material.vertices,source->material.vertices,sizeof(source->material.vertices))!=0) previousSource=NULL;
        }
        struct NativeSceneClipVertex vertices[3];
        struct NativeSceneSourceTriangle currentMaterial=SnapshotSourceForView(f,source,view),previousMaterial;
        if(previousSource) previousMaterial=SnapshotSourceForView(old,previousSource,view);
        struct NativeModelTriangle material=SnapshotMaterial(previousSource ? &previousMaterial:NULL,&currentMaterial,t,vertices);
        for(unsigned k=0;k<3;k++) {
            for(unsigned row=0;row<3;row++) {
                float eye=pose.position[row];
                for(unsigned col=0;col<3;col++) eye+=pose.basis[row][col]*(previousSource?SnapshotLerp(previousSource->local[k][col],source->local[k][col],t):source->local[k][col]);
                if(!isfinite(eye) || fabsf(eye)>(1<<24)) return NATIVE_ASSET_INVALID_DATA;
                vertices[k].view[row]=(s32)floorf(eye);
            }
        }
        unsigned outside[6]={0};
        s64 ox=clipCamera.transform.offset[0]/65536,oy=clipCamera.transform.offset[1]/65536;
        for(unsigned k=0;k<3;k++) {
            s64 x=vertices[k].view[0],y=vertices[k].view[1],z=vertices[k].view[2],h=clipCamera.transform.h;
            outside[0]+=z<clipCamera.nearDepth;outside[1]+=z>clipCamera.farDepth;
            outside[2]+=x*h+ox*z<0;outside[3]+=(s64)(clipCamera.width-ox)*z-x*h<0;
            outside[4]+=y*h+oy*z<0;outside[5]+=(s64)(clipCamera.height-oy)*z-y*h<0;
        }
        int rejected=0;for(unsigned plane=0;plane<6;plane++) if(outside[plane]==3) rejected=1;
        if(rejected) {stats->culled++;continue;}
        int cull=source->cull*(source->reflectedCull && viewDeterminant<0 ? -1:1);
        if(cull && !outside[0]) {
            double a[3],b[3];for(unsigned axis=0;axis<3;axis++) {a[axis]=(double)vertices[1].view[axis]-vertices[0].view[axis];b[axis]=(double)vertices[2].view[axis]-vertices[0].view[axis];}
            double winding=(a[1]*b[2]-a[2]*b[1])*vertices[0].view[0]+(a[2]*b[0]-a[0]*b[2])*vertices[0].view[1]+(a[0]*b[1]-a[1]*b[0])*vertices[0].view[2];
            if((cull>0 && winding<=0) || (cull<0 && winding>=0)) {stats->culled++;continue;}
        }
        struct NativeDrawTriangle output[NATIVE_SCENE_CLIP_MAX_TRIANGLES];size_t count;
        if(NativeSceneGeometry_NeedsClip(&clipCamera,vertices)) stats->clipped++;
        enum NativeAssetResult status=source->line ? NativeSceneGeometry_ClipLine(&clipCamera,&material,vertices,source->orderingBias,output,&count) :
            NativeSceneGeometry_ClipSubpixel(&clipCamera,&material,vertices,source->orderingBias,output,&count);
        if(status!=NATIVE_ASSET_OK) return status;
        if(!count) stats->culled++;
        for(size_t k=0;k<count;k++) {
            const struct NativeDrawTriangle *triangle=&output[k];
            float area=(triangle->visualXY[1][0]-triangle->visualXY[0][0])*(triangle->visualXY[2][1]-triangle->visualXY[0][1])-
                (triangle->visualXY[1][1]-triangle->visualXY[0][1])*(triangle->visualXY[2][0]-triangle->visualXY[0][0]);
            if((cull>0 && area<=0) || (cull<0 && area>=0)) {stats->culled++;continue;}
            unsigned depth=0;for(unsigned v=0;v<3;v++) if(triangle->depth[v]>depth) depth=triangle->depth[v];
            int bucket=(int)(depth>>6)+triangle->orderingBias;if(bucket<1) bucket=1;if(bucket>1022) bucket=1022;
            if(source->fixedBucket) bucket=(int)source->fixedBucket-1;
            if(sink && (status=sink(user,(unsigned)bucket,triangle))!=NATIVE_ASSET_OK) return status;
            stats->triangles++;
        }
    }
    stats->valid=1;return NATIVE_ASSET_OK;
}
