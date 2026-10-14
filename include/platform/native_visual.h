#ifndef NATIVE_VISUAL_H
#define NATIVE_VISUAL_H
#include <stddef.h>
#include <stdint.h>

/* Host-owned, value-only display snapshots. No historical game pointer is
 * dereferenced and no visual state is serialized into retail checkpoints. */
struct NativeVisualProjection {
    float matrix[3][3], translation[3], local[3], offset[2], h, maxRatio;
    unsigned terrainViewport;
    unsigned terrainClipped;
};
struct NativeVisualStats { unsigned packets, vertices, matched, sampled, projected; float alpha; int overflow; };
#ifdef CTR_NATIVE_VISUAL_INTERPOLATION
void NativeVisual_Reset(void);
void NativeVisual_NewObject(const void *object);
void NativeVisual_BeginCapture(int enabled, uint64_t signature);
void NativeVisual_Section(unsigned section);
void NativeVisual_Push(unsigned kind, const void *object, uint64_t part, int world);
void NativeVisual_Pop(void);
void NativeVisual_Primitive(uint64_t primitive);
void NativeVisual_Camera(unsigned viewport,const struct NativeVisualProjection *camera);
int NativeVisual_IsCapturing(void);
void NativeVisual_RecordPacket(const void *packet, unsigned words);
void NativeVisual_BindProjection(const void *field, const struct NativeVisualProjection *projection);
void NativeVisual_GteProject(const struct NativeVisualProjection *projection, uint32_t xy);
void NativeVisual_GteWrite(unsigned reg);
void NativeVisual_GteStore(const void *field, unsigned index, uint32_t xy);
void NativeVisual_TerrainProjection(const float view[3], struct NativeVisualProjection *out);
void NativeVisual_EndCapture(void);
void NativeVisual_BeginPresentation(float alpha);
void NativeVisual_EndPresentation(void);
void NativeVisual_SampleXY(const void *source, float *x, float *y);
void NativeVisual_SampleRGB(const void *source, uint8_t *r, uint8_t *g, uint8_t *b);
void NativeVisual_SampleRect(const void *source, float *w, float *h);
struct NativeVisualStats NativeVisual_GetStats(void);
#else
static inline void NativeVisual_Reset(void) {}
static inline void NativeVisual_NewObject(const void *o) { (void)o; }
static inline void NativeVisual_BeginCapture(int e,uint64_t s) { (void)e;(void)s; }
static inline void NativeVisual_Section(unsigned s) { (void)s; }
static inline void NativeVisual_Push(unsigned k,const void *o,uint64_t p,int w) { (void)k;(void)o;(void)p;(void)w; }
static inline void NativeVisual_Pop(void) {}
static inline void NativeVisual_Primitive(uint64_t p) { (void)p; }
static inline void NativeVisual_Camera(unsigned v,const struct NativeVisualProjection *c) { (void)v;(void)c; }
static inline int NativeVisual_IsCapturing(void) { return 0; }
static inline void NativeVisual_RecordPacket(const void *p,unsigned w) { (void)p;(void)w; }
static inline void NativeVisual_BindProjection(const void *f,const struct NativeVisualProjection *p) {(void)f;(void)p;}
static inline void NativeVisual_GteProject(const struct NativeVisualProjection *p,uint32_t xy) {(void)p;(void)xy;}
static inline void NativeVisual_GteWrite(unsigned r) {(void)r;}
static inline void NativeVisual_GteStore(const void *f,unsigned i,uint32_t xy) {(void)f;(void)i;(void)xy;}
static inline void NativeVisual_TerrainProjection(const float v[3],struct NativeVisualProjection *p) {(void)v;*p=(struct NativeVisualProjection){0};}
static inline void NativeVisual_EndCapture(void) {}
static inline void NativeVisual_BeginPresentation(float a) { (void)a; }
static inline void NativeVisual_EndPresentation(void) {}
static inline void NativeVisual_SampleXY(const void *s,float *x,float *y) { (void)s;(void)x;(void)y; }
static inline void NativeVisual_SampleRGB(const void *s,uint8_t *r,uint8_t *g,uint8_t *b) { (void)s;(void)r;(void)g;(void)b; }
static inline void NativeVisual_SampleRect(const void *s,float *w,float *h) { (void)s;(void)w;(void)h; }
static inline struct NativeVisualStats NativeVisual_GetStats(void) { return (struct NativeVisualStats){0}; }
#endif
#endif
