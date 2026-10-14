#include <platform/native_visual.h>
#include <platform/native_gpu_links.h>
#include <math.h>
#include <string.h>

/* Fixed budgets keep capture failure reversible: an oversized frame displays
 * its original packets instead of using a partially populated history. */
#define NV_PACKETS 16384u
#define NV_TABLE 65536u
#define NV_PROJECTIONS 65536u
#define NV_OBJECTS 16384u
#define NV_CONTEXTS 32768u
#define NV_FIELDS 131072u

struct NvContext {
    uint64_t key, baseKey; unsigned ordinal,terrainViewport; int world;
};
struct NvVertex {
    float xy[2],uv[2]; uint8_t rgb[3], xyOffset, rgbOffset,uvOffset; int projected;
    struct NativeVisualProjection projection;
};
struct NvPacket {
    uintptr_t address; uint64_t key, context, material;
    unsigned words, count, contextCount, terrainViewport; int previous,world;
    float width, height; struct NvVertex vertex[4];
};
struct NvFrame { unsigned count,cameraMask,terrainValidMask; struct NativeVisualProjection camera[4]; struct NvPacket packet[NV_PACKETS]; };
struct NvProjection { uintptr_t field; uint32_t xy; struct NativeVisualProjection value; int used; };
struct NvObject { uintptr_t address; uint64_t serial; };
struct NvCount { uint64_t key; unsigned count; int used; };
static struct NvFrame nv_frames[2];
static unsigned nv_current;
static uint32_t nv_addresses[NV_TABLE], nv_history[NV_TABLE];
static uint32_t nv_xyFields[NV_FIELDS], nv_rgbFields[NV_FIELDS],nv_uvFields[NV_FIELDS];
static struct NvProjection nv_projections[NV_PROJECTIONS];
static struct NvObject nv_objects[NV_OBJECTS];
static struct NvProjection nv_gte[3];
static struct NvCount nv_counts[NV_CONTEXTS];
static struct NvContext nv_context, nv_stack[8];
static unsigned nv_stackSize, nv_terrainMask;
static uint64_t nv_serial, nv_signature;
static int nv_capturing, nv_presenting, nv_historyValid, nv_overflow;
static struct NativeVisualStats nv_stats;

static uint64_t NvHash(uint64_t x)
{
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static float NvLerp(float a,float b,float t) { return a+(b-a)*t; }
static uint32_t NvWord(const void *p) { uint32_t w; memcpy(&w,p,4); return w; }
static float NvCoord(uint32_t word,unsigned axis) { return (float)(int16_t)(word>>(axis*16)); }

void NativeVisual_Reset(void)
{
    nv_capturing = nv_presenting = nv_historyValid = 0;
    nv_frames[0].count = nv_frames[1].count = nv_frames[0].cameraMask = nv_frames[1].cameraMask = 0;
    memset(nv_addresses,0,sizeof(nv_addresses));
    memset(nv_xyFields,0,sizeof(nv_xyFields));memset(nv_rgbFields,0,sizeof(nv_rgbFields));
    memset(nv_uvFields,0,sizeof(nv_uvFields));
    memset(&nv_stats,0,sizeof(nv_stats));
}
void NativeVisual_NewObject(const void *object)
{
    uintptr_t address=(uintptr_t)object;
    if(!address) return;
    unsigned slot=(unsigned)NvHash(address)&(NV_OBJECTS-1);
    for(unsigned n=0;n<NV_OBJECTS;n++,slot=(slot+1)&(NV_OBJECTS-1)) {
        if(!nv_objects[slot].address || nv_objects[slot].address==address) {
            nv_objects[slot]=(struct NvObject){address,++nv_serial}; return;
        }
    }
    NativeVisual_Reset(); memset(nv_objects,0,sizeof(nv_objects));
    NativeVisual_NewObject(object);
}
static uint64_t NvObjectID(const void *object)
{
    uintptr_t address=(uintptr_t)object;
    unsigned slot=(unsigned)NvHash(address)&(NV_OBJECTS-1);
    for(unsigned n=0;n<NV_OBJECTS;n++,slot=(slot+1)&(NV_OBJECTS-1)) {
        if(nv_objects[slot].address==address) return NvHash(address^nv_objects[slot].serial);
        if(!nv_objects[slot].address) break;
    }
    return NvHash(address); // Immutable scene owners; reload resets history.
}
void NativeVisual_BeginCapture(int enabled,uint64_t signature)
{
    if(!enabled) { NativeVisual_Reset(); return; }
    if(signature!=nv_signature) NativeVisual_Reset();
    nv_signature=signature;
    if(nv_frames[nv_current].count) nv_historyValid=1;
    nv_current^=1; nv_frames[nv_current].count=nv_frames[nv_current].cameraMask=0;
    memset(nv_addresses,0,sizeof(nv_addresses));
    memset(nv_projections,0,sizeof(nv_projections));
    memset(nv_gte,0,sizeof(nv_gte));
    memset(nv_counts,0,sizeof(nv_counts));
    memset(&nv_stats,0,sizeof(nv_stats));
    nv_stackSize=0; nv_overflow=0; nv_capturing=1;
    NativeVisual_Section(0);
}
void NativeVisual_Section(unsigned section)
{
    if(!nv_capturing) return;
    uint64_t key=NvHash((uint64_t)section+1);
    nv_context=(struct NvContext){.key=key,.baseKey=key}; nv_stackSize=0;
}
void NativeVisual_Push(unsigned kind,const void *object,uint64_t part,int world)
{
    if(!nv_capturing) return;
    if(nv_stackSize==8) { nv_overflow=1; return; }
    nv_stack[nv_stackSize++]=nv_context;
    uint64_t key=NvHash(NvHash(NvHash(nv_context.key^kind)^NvObjectID(object))^part);
    unsigned terrainViewport=kind==4 && part<4 ? (unsigned)part+1 : kind==3 ? nv_context.terrainViewport : 0;
    nv_context=(struct NvContext){.key=key,.baseKey=key,.world=world,.terrainViewport=terrainViewport};
}
void NativeVisual_Pop(void)
{
    if(nv_capturing && nv_stackSize) nv_context=nv_stack[--nv_stackSize];
}
void NativeVisual_Primitive(uint64_t primitive)
{
    if(!nv_capturing) return;
    nv_context.key=NvHash(nv_context.baseKey^primitive);nv_context.ordinal=0;
}
void NativeVisual_Camera(unsigned viewport,const struct NativeVisualProjection *camera)
{
    if(!nv_capturing || viewport>=4 || !camera) return;
    nv_frames[nv_current].camera[viewport]=*camera;nv_frames[nv_current].cameraMask|=1u<<viewport;
}
int NativeVisual_IsCapturing(void) { return nv_capturing && !nv_overflow; }
static unsigned NvCountSlot(uint64_t key)
{
    unsigned slot=(unsigned)NvHash(key)&(NV_CONTEXTS-1);
    for(unsigned n=0;n<NV_CONTEXTS;n++,slot=(slot+1)&(NV_CONTEXTS-1)) {
        if(!nv_counts[slot].used || nv_counts[slot].key==key) return slot;
    }
    nv_overflow=1; return 0;
}
void NativeVisual_RecordPacket(const void *packet,unsigned words)
{
    if(!nv_capturing || nv_overflow || words==0 || words>16) return;
    struct NvFrame *frame=&nv_frames[nv_current]; uintptr_t address=(uintptr_t)packet;
    unsigned slot=(unsigned)NvHash(address)&(NV_TABLE-1);
    while(nv_addresses[slot]) {
        if(frame->packet[nv_addresses[slot]-1].address==address) return;
        slot=(slot+1)&(NV_TABLE-1);
    }
    if(frame->count==NV_PACKETS) { nv_overflow=1; return; }
    struct NvPacket *p=&frame->packet[frame->count]; memset(p,0,sizeof(*p));
    p->address=address; p->context=nv_context.key; p->words=words; p->previous=-1;
    p->world=nv_context.world;p->terrainViewport=nv_context.terrainViewport;
    p->key=NvHash(nv_context.key^NvHash(++nv_context.ordinal));
    unsigned countSlot=NvCountSlot(p->context);
    nv_counts[countSlot].key=p->context; nv_counts[countSlot].used=1; nv_counts[countSlot].count++;
    nv_addresses[slot]=++frame->count;
}
/* Metadata follows the exact SXY FIFO slot and destination field. Equal
 * screen coordinates never identify a source vertex. Manual FIFO writes
 * invalidate their metadata rather than guessing which vertex was copied. */
void NativeVisual_GteProject(const struct NativeVisualProjection *p,uint32_t xy)
{
    if(!nv_capturing) return;
    nv_gte[0]=nv_gte[1];nv_gte[1]=nv_gte[2];
    nv_gte[2]=(struct NvProjection){.xy=xy,.used=p!=NULL};
    if(p) nv_gte[2].value=*p;
}
void NativeVisual_GteWrite(unsigned reg)
{
    if(!nv_capturing) return;
    if(reg==15) {nv_gte[0]=nv_gte[1];nv_gte[1]=nv_gte[2];nv_gte[2].used=0;}
    else if(reg>=12 && reg<=14) nv_gte[reg-12].used=0;
}
void NativeVisual_BindProjection(const void *field,const struct NativeVisualProjection *p)
{
    if(!nv_capturing || nv_overflow || !p || !field || !nv_context.world) return;
    uintptr_t address=(uintptr_t)field;
    unsigned slot=(unsigned)NvHash(address)&(NV_PROJECTIONS-1);
    for(unsigned n=0;n<NV_PROJECTIONS;n++,slot=(slot+1)&(NV_PROJECTIONS-1)) {
        struct NvProjection *entry=&nv_projections[slot];
        if(!entry->used || entry->field==address) {
            *entry=(struct NvProjection){.field=address,.xy=NvWord(field),.value=*p,.used=1};return;
        }
    }
    nv_overflow=1;
}
void NativeVisual_GteStore(const void *field,unsigned index,uint32_t xy)
{
    if(nv_capturing && index<3 && nv_gte[index].used && nv_gte[index].xy==xy)
        NativeVisual_BindProjection(field,&nv_gte[index].value);
}
static const struct NativeVisualProjection *NvProjectionFor(const void *field,uint32_t xy)
{
    uintptr_t address=(uintptr_t)field;
    unsigned slot=(unsigned)NvHash(address)&(NV_PROJECTIONS-1);
    for(unsigned n=0;n<NV_PROJECTIONS;n++,slot=(slot+1)&(NV_PROJECTIONS-1)) {
        const struct NvProjection *entry=&nv_projections[slot];
        if(!entry->used) return NULL;
        if(entry->field==address) return entry->xy==xy ? &entry->value : NULL;
    }
    return NULL;
}
void NativeVisual_TerrainProjection(const float view[3],struct NativeVisualProjection *out)
{
    *out=(struct NativeVisualProjection){0};
    if(!nv_capturing || !nv_context.terrainViewport || nv_overflow) return;
    unsigned viewport=nv_context.terrainViewport-1;
    if(!(nv_frames[nv_current].cameraMask&(1u<<viewport))) return;
    struct NativeVisualProjection value=nv_frames[nv_current].camera[viewport];
    const float (*m)[3]=value.matrix;
    float det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if(fabsf(det)<0.001f) return;
    float relative[3];for(unsigned i=0;i<3;i++) relative[i]=view[i]-value.translation[i];
    for(unsigned row=0;row<3;row++) {
        value.local[row]=0;
        for(unsigned col=0;col<3;col++) {
            unsigned a=(col+1)%3,b=(col+2)%3,c=(row+1)%3,d=(row+2)%3;
            value.local[row]+=(m[a][c]*m[b][d]-m[a][d]*m[b][c])*relative[col]/det;
        }
    }
    value.terrainViewport=nv_context.terrainViewport;
    value.terrainClipped=1;
    *out=value;
}
static int NvProjectPoint(const struct NativeVisualProjection *p,float xy[2]);
static void NvDecode(struct NvPacket *p)
{
    const uint8_t *raw=(const uint8_t *)p->address;
    if(!NativeGpuLinks_IsRegisteredHostRange(raw,(p->words+1)*4u)) return;
    unsigned code=raw[7]&0xfcu, xy[4]={0}, rgb[4]={4,4,4,4}, uv[4]={0};
    switch(code) {
    case 0x20: p->count=3; xy[0]=8;xy[1]=12;xy[2]=16;break;
    case 0x24: p->count=3; for(unsigned i=0;i<3;i++){xy[i]=8+i*8;uv[i]=12+i*8;} break;
    case 0x28: p->count=4; for(unsigned i=0;i<4;i++) xy[i]=8+i*4;break;
    case 0x2c: p->count=4; for(unsigned i=0;i<4;i++){xy[i]=8+i*8;uv[i]=12+i*8;} break;
    case 0x30: p->count=3; for(unsigned i=0;i<3;i++){xy[i]=8+i*8;rgb[i]=4+i*8;} break;
    case 0x34: p->count=3; for(unsigned i=0;i<3;i++){xy[i]=8+i*12;rgb[i]=4+i*12;uv[i]=12+i*12;} break;
    case 0x38: p->count=4; for(unsigned i=0;i<4;i++){xy[i]=8+i*8;rgb[i]=4+i*8;} break;
    case 0x3c: p->count=4; for(unsigned i=0;i<4;i++){xy[i]=8+i*12;rgb[i]=4+i*12;uv[i]=12+i*12;} break;
    case 0x40: p->count=2;xy[0]=8;xy[1]=12;break;
    case 0x50: p->count=2;xy[0]=8;xy[1]=16;rgb[1]=12;break;
    case 0x60: case 0x64: case 0x68: case 0x6c: case 0x70: case 0x74: case 0x78: case 0x7c:
        p->count=1;xy[0]=8;
        if(code&4) uv[0]=12;
        if((code&0x18)==0) {
            unsigned sizeOffset=(code&4)?16:12;
            if(sizeOffset+4>(p->words+1)*4u) {p->count=0;return;}
            uint32_t wh=NvWord(raw+sizeOffset);p->width=(float)(wh&65535);p->height=(float)(wh>>16);
        } else {p->width=p->height=(code&0x18)==8?1:(code&0x18)==16?8:16;}
        break;
    default: return;
    }
    p->material=code;
    for(unsigned i=0;i<p->count;i++) {
        if(xy[i]+4>(p->words+1)*4u || rgb[i]+3>(p->words+1)*4u || (uv[i] && uv[i]+4>(p->words+1)*4u)) {p->count=0;return;}
        struct NvVertex *v=&p->vertex[i];v->xyOffset=(uint8_t)xy[i];v->rgbOffset=(uint8_t)rgb[i];
        uint32_t word=NvWord(raw+xy[i]);v->xy[0]=NvCoord(word,0);v->xy[1]=NvCoord(word,1);
        memcpy(v->rgb,raw+rgb[i],3);
        const struct NativeVisualProjection *projection=NvProjectionFor(raw+xy[i],word);
        if(projection) {
            float expected[2];
            // Saturated GTE output and specialized depth paths may not match
            // the captured affine pose. Never amplify that discrepancy.
            if(NvProjectPoint(projection,expected) &&
                fabsf(expected[0]-v->xy[0])<=4 && fabsf(expected[1]-v->xy[1])<=4) {
                v->projection=*projection;v->projected=1;
            }
        }
        // Texture page/CLUT and 2D icon identity change discretely. UV animation
        // of projected geometry does not invalidate its transform history.
        if(uv[i]) {
            v->uvOffset=(uint8_t)uv[i];v->uv[0]=raw[uv[i]];v->uv[1]=raw[uv[i]+1];
            uint32_t word=NvWord(raw+uv[i]);
            uint32_t identity=p->world ? (i<2 ? word>>16 : 0) : (word&65535)|(i<2 ? word&0xffff0000u : 0);
            p->material=NvHash(p->material^identity^i);
        }
    }
}
void NativeVisual_EndCapture(void)
{
    if(!nv_capturing) return;
    nv_capturing=0; struct NvFrame *frame=&nv_frames[nv_current],*old=&nv_frames[nv_current^1];
    nv_stats.overflow=nv_overflow;
    if(nv_overflow) { nv_historyValid=0;frame->count=0;memset(nv_addresses,0,sizeof(nv_addresses));return; }
    frame->terrainValidMask=frame->cameraMask;
    memset(nv_history,0,sizeof(nv_history));
    memset(nv_xyFields,0,sizeof(nv_xyFields));memset(nv_rgbFields,0,sizeof(nv_rgbFields));
    memset(nv_uvFields,0,sizeof(nv_uvFields));
    if(nv_historyValid) for(unsigned i=0;i<old->count;i++) {
        unsigned slot=(unsigned)NvHash(old->packet[i].key)&(NV_TABLE-1);
        while(nv_history[slot]) slot=(slot+1)&(NV_TABLE-1);
        nv_history[slot]=i+1;
    }
    for(unsigned i=0;i<frame->count;i++) {
        struct NvPacket *p=&frame->packet[i];NvDecode(p);
        if(p->terrainViewport) for(unsigned k=0;k<p->count;k++)
            if(!p->vertex[k].projected) frame->terrainValidMask&=~(1u<<(p->terrainViewport-1));
        for(unsigned k=0;k<p->count;k++) for(unsigned color=0;color<3;color++) {
            unsigned offset=color==2?p->vertex[k].uvOffset:color?p->vertex[k].rgbOffset:p->vertex[k].xyOffset;
            if(!offset) continue;
            uintptr_t address=p->address+offset;
            uint32_t *fields=color==2?nv_uvFields:color?nv_rgbFields:nv_xyFields;
            unsigned fieldSlot=(unsigned)NvHash(address)&(NV_FIELDS-1);
            while(fields[fieldSlot]) {
                unsigned record=fields[fieldSlot]-1,index=record>>2,v=record&3;
                const struct NvPacket *existing=&frame->packet[index];
                unsigned existingOffset=color==2?existing->vertex[v].uvOffset:color?existing->vertex[v].rgbOffset:existing->vertex[v].xyOffset;
                if(existing->address+existingOffset==address) break;
                fieldSlot=(fieldSlot+1)&(NV_FIELDS-1);
            }
            fields[fieldSlot]=(i<<2)+k+1;
        }
        p->contextCount=nv_counts[NvCountSlot(p->context)].count;
        nv_stats.vertices+=p->count;
        unsigned slot=(unsigned)NvHash(p->key)&(NV_TABLE-1);
        while(nv_history[slot]) {
            unsigned previous=nv_history[slot]-1;const struct NvPacket *q=&old->packet[previous];
            if(q->key==p->key) {
                if(q->material==p->material && q->count==p->count && q->contextCount==p->contextCount) {
                    int continuous=1;
                    for(unsigned k=0;k<p->count;k++) for(unsigned axis=0;axis<2;axis++)
                        if(fabsf(p->vertex[k].xy[axis]-q->vertex[k].xy[axis])>256) continuous=0;
                    if(continuous) { p->previous=(int)previous;nv_stats.matched+=p->count; }
                }
                break;
            }
            slot=(slot+1)&(NV_TABLE-1);
        }
    }
    nv_stats.packets=frame->count;
}

/* Decompose an orthogonal scaled basis. Reject reflections/shear; those use
 * bounded screen-space interpolation rather than an invalid quaternion. */
static int NvQuaternion(const float matrix[3][3],float q[4],float scale[3])
{
    float r[3][3];
    for(unsigned col=0;col<3;col++) {
        float length=0;for(unsigned row=0;row<3;row++) length+=matrix[row][col]*matrix[row][col];
        scale[col]=sqrtf(length);if(scale[col]<0.00001f) return 0;
        for(unsigned row=0;row<3;row++) r[row][col]=matrix[row][col]/scale[col];
    }
    for(unsigned a=0;a<3;a++) for(unsigned b=a+1;b<3;b++) {
        float dot=0;for(unsigned row=0;row<3;row++) dot+=r[row][a]*r[row][b];
        if(fabsf(dot)>0.02f) return 0;
    }
    float determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if(determinant<0.9f) return 0;
    float trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0) {
        float s=sqrtf(trace+1)*2;q[3]=0.25f*s;q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s;
    } else {
        unsigned a=0;if(r[1][1]>r[a][a]) a=1;if(r[2][2]>r[a][a]) a=2;
        unsigned b=(a+1)%3,c=(a+2)%3;float s=sqrtf(1+r[a][a]-r[b][b]-r[c][c])*2;
        q[a]=0.25f*s;q[b]=(r[a][b]+r[b][a])/s;q[c]=(r[a][c]+r[c][a])/s;q[3]=(r[c][b]-r[b][c])/s;
    }
    return 1;
}
static int NvProjectBlend(const struct NvVertex *a,const struct NvVertex *b,float t,float xy[2])
{
    const struct NativeVisualProjection *pa=&a->projection,*pb=&b->projection;
    float qa[4],qb[4],sa[3],sb[3];
    if(!a->projected || !b->projected) return 0;
    int rigid=NvQuaternion(pa->matrix,qa,sa) && NvQuaternion(pb->matrix,qb,sb);
    if(!rigid) {
        float view[3];for(unsigned row=0;row<3;row++) {
            view[row]=NvLerp(pa->translation[row],pb->translation[row],t);
            for(unsigned col=0;col<3;col++) view[row]+=NvLerp(pa->matrix[row][col],pb->matrix[row][col],t)*NvLerp(pa->local[col],pb->local[col],t);
        }
        if(view[2]<=0) return 0;
        float ratio=NvLerp(pa->h,pb->h,t)/view[2],limit=pb->maxRatio>0?pb->maxRatio:2;
        if(ratio>limit) ratio=limit;
        for(unsigned axis=0;axis<2;axis++) xy[axis]=NvLerp(pa->offset[axis],pb->offset[axis],t)+view[axis]*ratio;
        return isfinite(xy[0]) && isfinite(xy[1]);
    }
    float dot=0;for(unsigned i=0;i<4;i++) dot+=qa[i]*qb[i];
    if(dot<0) { dot=-dot;for(unsigned i=0;i<4;i++) qb[i]=-qb[i]; }
    if(dot>1) dot=1;
    float wa=1-t,wb=t;
    if(dot<0.9995f) {float angle=acosf(dot),den=sinf(angle);wa=sinf((1-t)*angle)/den;wb=sinf(t*angle)/den;}
    float q[4],norm=0;for(unsigned i=0;i<4;i++){q[i]=qa[i]*wa+qb[i]*wb;norm+=q[i]*q[i];}
    norm=sqrtf(norm);for(unsigned i=0;i<4;i++) q[i]/=norm;
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float r[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
        {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
        {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    float local[3],view[3];
    for(unsigned i=0;i<3;i++) {
        if(fabsf(pa->translation[i]-pb->translation[i])>8192) return 0;
        local[i]=NvLerp(pa->local[i],pb->local[i],t)*NvLerp(sa[i],sb[i],t);
    }
    for(unsigned row=0;row<3;row++) {
        view[row]=NvLerp(pa->translation[row],pb->translation[row],t);
        for(unsigned col=0;col<3;col++) view[row]+=r[row][col]*local[col];
    }
    if(view[2]<=0) return 0;
    float ratio=NvLerp(pa->h,pb->h,t)/view[2],limit=pb->maxRatio>0?pb->maxRatio:2;if(ratio>limit) ratio=limit;
    for(unsigned axis=0;axis<2;axis++) {
        xy[axis]=NvLerp(pa->offset[axis],pb->offset[axis],t)+view[axis]*ratio;
        if(!isfinite(xy[axis]) || xy[axis]<-1024 || xy[axis]>1023) return 0;
    }
    return 1;
}
static int NvProjectPoint(const struct NativeVisualProjection *p,float xy[2])
{
    float view[3];
    for(unsigned row=0;row<3;row++) {
        view[row]=p->translation[row];
        for(unsigned col=0;col<3;col++) view[row]+=p->matrix[row][col]*p->local[col];
    }
    if(view[2]<=0) return 0;
    float ratio=p->h/view[2],limit=p->maxRatio>0?p->maxRatio:2;if(ratio>limit) ratio=limit;
    for(unsigned axis=0;axis<2;axis++) xy[axis]=p->offset[axis]+view[axis]*ratio;
    return isfinite(xy[0]) && isfinite(xy[1]);
}
void NativeVisual_BeginPresentation(float alpha)
{
    nv_stats.alpha=isfinite(alpha)?fminf(1,fmaxf(0,alpha)):1;
    nv_stats.sampled=nv_stats.projected=0;
    nv_presenting=nv_historyValid && !nv_overflow && nv_stats.alpha<1;
    struct NvFrame *frame=&nv_frames[nv_current],*old=&nv_frames[nv_current^1];
    nv_terrainMask=frame->terrainValidMask & old->terrainValidMask;
    if(!nv_presenting) return;
    // A terrain vertex crossing behind the old camera cannot be reprojected.
    // Fall back for the whole viewport, so adjacent triangles never choose
    // different cameras or mix canonical and interpolated corners.
    for(unsigned i=0;i<frame->count;i++) {
        const struct NvPacket *p=&frame->packet[i];
        if(!p->terrainViewport || !(nv_terrainMask&(1u<<(p->terrainViewport-1)))) continue;
        for(unsigned k=0;k<p->count;k++) {
            struct NvVertex a=p->vertex[k],b=p->vertex[k];float xy[2];
            unsigned viewport=p->terrainViewport-1;
            a.projection=old->camera[viewport];b.projection=frame->camera[viewport];
            memcpy(a.projection.local,p->vertex[k].projection.local,sizeof(a.projection.local));
            memcpy(b.projection.local,p->vertex[k].projection.local,sizeof(b.projection.local));
            if(!NvProjectPoint(&a.projection,xy) || !NvProjectBlend(&a,&b,nv_stats.alpha,xy)) {
                nv_terrainMask&=~(1u<<viewport);break;
            }
        }
    }
}
void NativeVisual_EndPresentation(void) { nv_presenting=0; }
static int NvTerrainHistory(const struct NvVertex *v)
{
    unsigned viewport=v->projection.terrainViewport;
    return v->projected && viewport>0 && viewport<=4 &&
        (nv_terrainMask & (1u<<(viewport-1)));
}
static const struct NvPacket *NvPacketAtField(const void *field,unsigned *vertex,int color)
{
    uintptr_t address=(uintptr_t)field;
    const uint32_t *fields=color==2?nv_uvFields:color?nv_rgbFields:nv_xyFields;
    unsigned slot=(unsigned)NvHash(address)&(NV_FIELDS-1);
    while(fields[slot]) {
        unsigned record=fields[slot]-1,k=record&3;
        const struct NvPacket *p=&nv_frames[nv_current].packet[record>>2];
        unsigned offset=color==2?p->vertex[k].uvOffset:color?p->vertex[k].rgbOffset:p->vertex[k].xyOffset;
        if(p->address+offset==address) {
            if(p->previous>=0 || (!color && NvTerrainHistory(&p->vertex[k]))) {*vertex=k;return p;} return NULL;
        }
        slot=(slot+1)&(NV_FIELDS-1);
    }
    return NULL;
}
void NativeVisual_SampleXY(const void *source,float *x,float *y)
{
    if(!nv_presenting || !source || !x || !y) return;
    unsigned k;const struct NvPacket *p=NvPacketAtField(source,&k,0);if(!p) return;
    if(p->terrainViewport && !(nv_terrainMask&(1u<<(p->terrainViewport-1)))) return;
    if(NvTerrainHistory(&p->vertex[k])) {
        const struct NvVertex *vertex=&p->vertex[k];unsigned viewport=vertex->projection.terrainViewport-1;
        struct NvVertex a=*vertex,b=*vertex;
        a.projection=nv_frames[nv_current^1].camera[viewport];b.projection=nv_frames[nv_current].camera[viewport];
        memcpy(a.projection.local,vertex->projection.local,sizeof(a.projection.local));
        memcpy(b.projection.local,vertex->projection.local,sizeof(b.projection.local));
        float xy[2],previous[2],current[2],t=nv_stats.alpha;
        if(NvProjectBlend(&a,&b,t,xy) && NvProjectPoint(&a.projection,previous) && NvProjectPoint(&b.projection,current)) {
            for(unsigned axis=0;axis<2;axis++) {
                xy[axis]+=NvLerp(floorf(previous[axis])-previous[axis],vertex->xy[axis]-current[axis],t);
                float boundary=b.projection.offset[axis]*2;
                if(vertex->projection.terrainClipped && (vertex->xy[axis]==0 || vertex->xy[axis]==boundary)) xy[axis]=vertex->xy[axis];
            }
            *x=xy[0];*y=xy[1];nv_stats.projected++;nv_stats.sampled++;
        }
        return;
    }
    const struct NvPacket *old=&nv_frames[nv_current^1].packet[p->previous];
    const struct NvVertex *a=&old->vertex[k],*b=&p->vertex[k];float t=nv_stats.alpha,xy[2];
    if(p->world) {
        for(unsigned i=0;i<p->count;i++)
            if(!p->vertex[i].projected || !old->vertex[i].projected) return;
        for(unsigned i=0;i<p->count;i++) {
            float candidate[2];
            if(!NvProjectBlend(&old->vertex[i],&p->vertex[i],t,candidate)) return;
        }
    }
    if(t>0 && NvProjectBlend(a,b,t,xy)) {
        float previous[2],current[2];
        if(NvProjectPoint(&a->projection,previous) && NvProjectPoint(&b->projection,current))
            for(unsigned axis=0;axis<2;axis++) xy[axis]+=NvLerp(a->xy[axis]-previous[axis],b->xy[axis]-current[axis],t);
        *x=xy[0];*y=xy[1];nv_stats.projected++;
    } else {*x=NvLerp(a->xy[0],b->xy[0],t);*y=NvLerp(a->xy[1],b->xy[1],t);}
    nv_stats.sampled++;
}
void NativeVisual_SampleRGB(const void *source,uint8_t *r,uint8_t *g,uint8_t *b)
{
    if(!nv_presenting || !source) return;
    unsigned k;const struct NvPacket *p=NvPacketAtField(source,&k,1);if(!p || p->world) return;
    const struct NvPacket *old=&nv_frames[nv_current^1].packet[p->previous];
    uint8_t *out[3]={r,g,b};for(unsigned i=0;i<3;i++) *out[i]=(uint8_t)lroundf(NvLerp(old->vertex[k].rgb[i],p->vertex[k].rgb[i],nv_stats.alpha));
}
void NativeVisual_SampleRect(const void *source,float *w,float *h)
{
    if(!nv_presenting || !source || !w || !h) return;
    unsigned k;const struct NvPacket *p=NvPacketAtField(source,&k,0);if(!p || p->count!=1) return;
    const struct NvPacket *old=&nv_frames[nv_current^1].packet[p->previous];
    *w=NvLerp(old->width,p->width,nv_stats.alpha);*h=NvLerp(old->height,p->height,nv_stats.alpha);
}
struct NativeVisualStats NativeVisual_GetStats(void) { return nv_stats; }
