#include <platform/native_visual.h>
#include <platform/native_gpu_links.h>
#include <platform/native_gpu_vertices.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static POLY_G3 triangles[2];
static TILE rectangles[2];
static int identity;
void DrawEnvOffset(float *x,float *y) { *x=3;*y=5; }
static void Register(void)
{
    NativeGpuLinks_Reset();
    CHECK(NativeGpuLinks_RegisterRange(triangles,sizeof(triangles),NULL));
    CHECK(NativeGpuLinks_RegisterRange(rectangles,sizeof(rectangles),NULL));
}
static void Capture(unsigned index,int position,int color,float angle,int projected,uint64_t signature)
{
    NativeVisual_BeginCapture(1,signature);NativeVisual_Section(10);
    NativeVisual_Push(1,&identity,0,projected);
    POLY_G3 *p=&triangles[index];memset(p,0,sizeof(*p));setPolyG3(p);
    VERTTYPE *xy[3]={&p->x0,&p->x1,&p->x2};
    uint8_t *rgb[3]={&p->r0,&p->r1,&p->r2};
    for(unsigned i=0;i<3;i++) {
        struct NativeVisualProjection pose={.translation={0,0,256},.h=256};
        pose.matrix[0][0]=pose.matrix[1][1]=cosf(angle);
        pose.matrix[0][1]=-sinf(angle);pose.matrix[1][0]=sinf(angle);pose.matrix[2][2]=1;
        pose.local[0]=100+(float)i*5;pose.local[1]=(float)i*10;
        xy[i][0]=(short)(projected ? lroundf(pose.matrix[0][0]*pose.local[0]+pose.matrix[0][1]*pose.local[1]) : position+(int)i*5);
        xy[i][1]=(short)(projected ? lroundf(pose.matrix[1][0]*pose.local[0]+pose.matrix[1][1]*pose.local[1]) : 50+(int)i*5);
        rgb[i][0]=rgb[i][1]=rgb[i][2]=(uint8_t)color;
        if(projected) NativeVisual_BindProjection(xy[i],&pose);
    }
    p->code=0x30;NativeVisual_Pop();NativeVisual_Section(20);
    TILE *r=&rectangles[index];memset(r,0,sizeof(*r));setTile(r);
    r->x0=(short)position;r->y0=10;r->w=(short)(position/10);r->h=8;r->r0=(uint8_t)color;
    NativeVisual_EndCapture();
}
static void CaptureTerrain(unsigned frame,float cameraShift,int missing)
{
    NativeVisual_BeginCapture(1,30);
    struct NativeVisualProjection camera={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={cameraShift,0,0},.h=256,.maxRatio=4};
    NativeVisual_Camera(0,&camera);NativeVisual_Push(4,&identity,0,1);NativeVisual_Push(3,&identity,0,1);
    POLY_G3 *p=&triangles[frame];memset(p,0,sizeof(*p));setPolyG3(p);
    VERTTYPE *fields[3]={&p->x0,&p->x1,&p->x2};
    for(unsigned k=0;k<3;k++) {
        float view[3]={10+(float)k*5,20+(float)k*5,256};
        struct NativeVisualProjection pose;
        fields[k][0]=(short)view[0];fields[k][1]=(short)view[1];
        NativeVisual_TerrainProjection(view,&pose);
        if(!missing || k!=0) NativeVisual_BindProjection(fields[k],&pose);
    }
    NativeVisual_Pop();NativeVisual_Pop();NativeVisual_EndCapture();
}
static GrVertex Draw(float alpha)
{
    POLY_G3 *p=&triangles[1];GrVertex output[4];
    NativeVisual_BeginPresentation(alpha);
    MakeVertexTriangle(output,&p->x0,&p->x1,&p->x2);
    MakeColourTriangle(output,true,&p->r0,&p->r1,&p->r2);
    NativeVisual_EndPresentation();return output[0];
}
static void EffectBindings(void)
{
    NativeVisual_Reset();NativeVisual_BeginCapture(1,900);
    NativeVisual_Push(8,&identity,0,1);NativeVisual_EffectScope(0,0.25f,NATIVE_EFFECT_STAGED,NULL);
    POLY_G3 candidate={0};candidate.code=0x30;
    VERTTYPE *fields[3]={&candidate.x0,&candidate.x1,&candidate.x2};
    for(unsigned k=0;k<3;k++) {
        struct NativeVisualProjection pose={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={0,0,-256},.local={k*10.0f,0,0},.h=128};
        fields[k][0]=fields[k][1]=0;
        NativeVisual_BindProjection(fields[k],&pose);
    }
    CHECK(NativeVisual_StageEffect(&candidate,6,12,0xa40));
    POLY_G3 *live=&triangles[0];*live=candidate;
    NativeVisual_CopyProjection(&live->x0,&candidate.x0);NativeVisual_CopyProjection(&live->x1,&candidate.x1);NativeVisual_CopyProjection(&live->x2,&candidate.x2);
    NativeVisual_RecordPacket(live,6);memset(&candidate,0,sizeof(candidate));
    NativeVisual_EndCapture();
    struct NativeSceneEffect copied;
    CHECK(NativeVisual_StagedEffect(0,&copied) && copied.count==3 && copied.eye[0][2]==-64 && copied.eye[2][0]==5);
    CHECK(NativeVisual_EffectCovered(live)); /* Even a currently invisible face has a source. */
    CHECK(!NativeVisual_StagedEffect(1,&copied));NativeVisual_Reset();CHECK(!NativeVisual_StagedEffect(0,&copied));
    /* A compound draw-mode + gradient line has six payload words. */
    uint32_t line[7]={0,0xe1000a40,0,0x52010203,0,0x00040506,0};
    CHECK(NativeGpuLinks_RegisterRange(line,sizeof(line),NULL));
    NativeVisual_BeginCapture(1,901);NativeVisual_Push(2,&identity,0,1);NativeVisual_EffectScope(0,1,0,NULL);
    for(unsigned k=0;k<2;k++) {
        struct NativeVisualProjection pose={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={0,0,256},.local={k*10.0f,0,0},.h=256};
        line[4+k*2]=k*10;NativeVisual_BindProjection(&line[4+k*2],&pose);
    }
    NativeVisual_RecordPacket(line,6);NativeVisual_EndCapture();
    CHECK(NativeVisual_PacketEffect(line,&copied) && copied.count==2 && copied.code==0x52 && copied.page==0xa40);
    CHECK(copied.colors[0]==0x010203 && copied.colors[1]==0x040506);
    NativeVisual_Reset();
    NativeVisual_BeginCapture(1,902);NativeVisual_Push(5,&identity,0,1);NativeVisual_EffectScope(0,1,0,NULL);
    POLY_G3 *p=&triangles[0];memset(p,0,sizeof(*p));p->code=0x30;
    VERTTYPE *corners[3]={&p->x0,&p->x1,&p->x2};
    for(unsigned k=0;k<3;k++) {
        struct NativeVisualProjection pose={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={0,0,256},.h=256};
        NativeVisual_BindProjection(corners[k],&pose);
    }
    uint32_t unbound=0;NativeVisual_CopyProjection(&p->x0,&unbound);
    NativeVisual_RecordPacket(p,6);NativeVisual_EndCapture();
    CHECK(!NativeVisual_PacketEffect(p,&copied)); /* Equal XY cannot revive stale provenance. */
    NativeVisual_Reset();
}
int main(void)
{
    Register();EffectBindings();
    Register();NativeVisual_NewObject(&identity);
    Capture(0,100,0,0,0,1);Capture(1,201,100,0,0,1);
    POLY_G3 original=triangles[1];TILE originalRect=rectangles[1];
    for(unsigned i=0;i<=120;i++) {
        float alpha=(float)i/120;GrVertex v=Draw(alpha);
        CHECK(fabsf(v.x-(103+101*alpha))<0.001f && v.y==55);
        CHECK(v.r==(unsigned)lroundf(100*alpha));
        CHECK(memcmp(&original,&triangles[1],sizeof(original))==0);
    }
    NativeVisual_BeginPresentation(0.5f);GrVertex rect[4];
    MakeVertexRect(rect,&rectangles[1].x0,rectangles[1].w,rectangles[1].h);
    CHECK(fabsf(rect[2].x-rect[0].x-15)<0.001f);
    NativeVisual_EndPresentation();CHECK(memcmp(&originalRect,&rectangles[1],sizeof(originalRect))==0);
    NativeVisual_Reset();Capture(0,0,0,0,1,1);Capture(1,0,0,3.14159265358979323846f,1,1);
    GrVertex rotated=Draw(0.5f);
    CHECK(fabsf(rotated.x-3)<0.01f && fabsf(fabsf(rotated.y-5)-100)<0.01f);
    CHECK(NativeVisual_GetStats().projected==3);
    NativeVisual_Reset();
    Capture(0,0,0,3.124139361f,1,1);Capture(1,0,0,-3.124139361f,1,1);
    rotated=Draw(0.5f);CHECK(fabsf(rotated.x+97)<0.03f && fabsf(rotated.y-5)<0.01f);
    NativeVisual_NewObject(&identity);Capture(1,200,0,0,0,1);
    CHECK(Draw(0).x==203); // A reused pool address cannot match its former object.
    Capture(0,100,0,0,0,2);Capture(1,200,0,0,0,3);
    CHECK(Draw(0).x==203); // Level/pause/camera signature discontinuity.
    NativeVisual_Reset();Capture(0,100,0,0,0,1);Capture(1,200,0,0,0,1);
    CHECK(Draw(NAN).x==203);CHECK(Draw(-1).x==103);CHECK(Draw(2).x==203);
    NativeVisual_Reset();CHECK(Draw(0).x==203);
    // Coincident screen vertices can have different source poses. Only the
    // exact destination field owns its projection, even when XY is identical.
    NativeVisual_Reset();
    for(unsigned frame=0;frame<2;frame++) {
        NativeVisual_BeginCapture(1,20);NativeVisual_Push(1,&identity,0,1);
        POLY_G3 *p=&triangles[frame];memset(p,0,sizeof(*p));setPolyG3(p);
        VERTTYPE *fields[3]={&p->x0,&p->x1,&p->x2};
        for(unsigned k=0;k<3;k++) {
            struct NativeVisualProjection pose={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={0,0,256},.h=256};
            pose.local[0]=frame ? 0 : (float)(k*20); fields[k][0]=(short)pose.local[0];fields[k][1]=0;
            NativeVisual_GteProject(&pose,(uint16_t)fields[k][0]);
            NativeVisual_GteStore(fields[k],2,(uint16_t)fields[k][0]);
        }
        NativeVisual_Pop();NativeVisual_EndCapture();
    }
    NativeVisual_BeginPresentation(0.5f);GrVertex sameXY[3];
    MakeVertexTriangle(sameXY,&triangles[1].x0,&triangles[1].x1,&triangles[1].x2);
    CHECK(sameXY[0].x==3 && sameXY[1].x==13 && sameXY[2].x==23);
    CHECK(NativeVisual_GetStats().projected==3);NativeVisual_EndPresentation();
    // Manual SXY writes have no trusted pose. Incomplete metadata keeps the
    // entire world primitive canonical rather than distorting one corner.
    NativeVisual_BeginCapture(1,20);NativeVisual_Push(1,&identity,0,1);
    setPolyG3(&triangles[1]);
    struct NativeVisualProjection pose={.matrix={{1,0,0},{0,1,0},{0,0,1}},.translation={0,0,256},.h=256};
    NativeVisual_GteProject(&pose,0);NativeVisual_GteWrite(14);
    NativeVisual_GteStore(&triangles[1].x0,2,0);
    NativeVisual_Pop();NativeVisual_EndCapture();
    NativeVisual_BeginPresentation(0.5f);
    MakeVertexTriangle(sameXY,&triangles[1].x0,&triangles[1].x1,&triangles[1].x2);
    CHECK(sameXY[0].x==3 && sameXY[1].x==3 && sameXY[2].x==3);
    CHECK(NativeVisual_GetStats().projected==0);NativeVisual_EndPresentation();
    // Terrain shares the camera pose even when packet matching changes. One
    // unverified corner disables reprojection for its entire viewport.
    NativeVisual_Reset();CaptureTerrain(0,0,0);CaptureTerrain(1,32,0);
    CHECK(fabsf(Draw(0.5f).x+3)<0.001f && NativeVisual_GetStats().projected==3);
    NativeVisual_Reset();CaptureTerrain(0,0,0);CaptureTerrain(1,32,1);
    CHECK(Draw(0.5f).x==13 && NativeVisual_GetStats().projected==0);
    NativeVisual_Reset();
    // A freshly rebuilt GPU arena has presentation-local subpixel coordinates;
    // its fields must never be paired with the canonical arena or survive it.
    NativeVisual_Reset();NativeVisual_EnableWorldSampling(0);NativeVisual_BeginPresentation(1);
    CHECK(NativeVisual_PresentationXY(&triangles[1].x0,12.25f,34.5f));
    MakeVertexTriangle(sameXY,&triangles[1].x0,&triangles[1].x1,&triangles[1].x2);
    CHECK(sameXY[0].x==15.25f && sameXY[0].y==39.5f);
    NativeVisual_EndPresentation();
    MakeVertexTriangle(sameXY,&triangles[1].x0,&triangles[1].x1,&triangles[1].x2);
    CHECK(sameXY[0].x==triangles[1].x0+3 && sameXY[0].y==triangles[1].y0+5);
    NativeVisual_EnableWorldSampling(1);
    // Previous arenas can disappear: history consists only of copied values.
    POLY_G3 *temporary=malloc(sizeof(*temporary));CHECK(temporary);
    CHECK(NativeGpuLinks_RegisterRange(temporary,sizeof(*temporary),NULL));
    *temporary=triangles[1];NativeVisual_BeginCapture(1,9);NativeVisual_Section(10);
    NativeVisual_RecordPacket(temporary,6);NativeVisual_EndCapture();free(temporary);
    NativeVisual_BeginCapture(1,9);NativeVisual_Section(10);
    NativeVisual_RecordPacket(&triangles[1],6);NativeVisual_EndCapture();(void)Draw(0.5f);
    unsigned count=16385;POLY_F3 *large=calloc(count,sizeof(*large));CHECK(large);
    NativeGpuLinks_Reset();CHECK(NativeGpuLinks_RegisterRange(large,count*sizeof(*large),NULL));
    NativeVisual_Reset();NativeVisual_BeginCapture(1,1);
    for(unsigned i=0;i<count;i++) NativeVisual_RecordPacket(&large[i],4);
    NativeVisual_EndCapture();CHECK(NativeVisual_GetStats().overflow && NativeVisual_GetStats().packets==0);
    NativeVisual_Reset();free(large);
    puts("Visual snapshots: GPU subpixel vertices, quaternion rotation/wrap, RGB/rect smoothing, generation/reset/lifetime safety and overflow pass.");
    return 0;
}
