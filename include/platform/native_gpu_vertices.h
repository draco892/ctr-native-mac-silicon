#ifndef NATIVE_GPU_VERTICES_H
#define NATIVE_GPU_VERTICES_H
#include <platform/native_renderer_types.h>
#include <stdbool.h>
void MakeLineArray(GrVertex *v,VERTTYPE *a,VERTTYPE *b);
void MakeVertexTriangle(GrVertex *v,VERTTYPE *a,VERTTYPE *b,VERTTYPE *c);
void MakeVertexQuad(GrVertex *v,VERTTYPE *a,VERTTYPE *b,VERTTYPE *c,VERTTYPE *d);
void MakeVertexRect(GrVertex *v,VERTTYPE *a,s16 w,s16 h);
void MakeColourNoShade(GrVertex *v,int count);
void MakeColourLine(GrVertex *v,bool shade,u8 *a,u8 *b);
void MakeColourTriangle(GrVertex *v,bool shade,u8 *a,u8 *b,u8 *c);
void MakeColourQuad(GrVertex *v,bool shade,u8 *a,u8 *b,u8 *c,u8 *d);
#endif
