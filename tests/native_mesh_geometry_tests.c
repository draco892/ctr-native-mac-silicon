#include <platform/native_mesh_geometry.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"failed: %s line %d\n",#c,__LINE__); return 1; } } while(0)
static void Put16(u8 *p,u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
int main(void)
{
    // Unaligned wire spans, extreme signed coordinates, no struct overlays.
    u8 vertexStorage[1+9*NATIVE_VERTEX_BYTES]={0},quadStorage[1+NATIVE_QUAD_BYTES]={0};
    u8 *vertices=vertexStorage+1,*quads=quadStorage+1;
    for(unsigned i=0;i<9;i++) {
        Put16(quads+2*i,(u16)i);
        Put16(vertices+16*i,(u16)(i==0 ? 0x8000 : i*100));
        Put16(vertices+16*i+2,0xffff); Put16(vertices+16*i+4,0x7fff);
        Put16(vertices+16*i+6,0x8001);
        CTR_WriteU32LE(vertices+16*i+8,0x12345600+i);
        CTR_WriteU32LE(vertices+16*i+12,0xabcdef00+i);
    }
    Put16(quads+0x12,0x1000); CTR_WriteU32LE(quads+0x14,0x80000000); CTR_WriteU32LE(quads+0x18,0x12345678);
    u8 vertexSnapshot[sizeof(vertexStorage)],quadSnapshot[sizeof(quadStorage)];
    memcpy(vertexSnapshot,vertexStorage,sizeof(vertexStorage)); memcpy(quadSnapshot,quadStorage,sizeof(quadStorage));
    struct NativeMeshView mesh={.quadCount=1,.vertexCount=9,.quads=quads,.vertices=vertices};
    struct NativeMeshVertex vertex; struct NativeMeshQuad quad; struct NativeMeshTriangle triangle;
    CHECK(NativeMesh_GetVertex(&mesh,0,&vertex)==NATIVE_ASSET_OK);
    CHECK(vertex.position[0]==INT16_MIN && vertex.position[1]==-1 && vertex.position[2]==INT16_MAX && vertex.flags==0x8001);
    CHECK(vertex.colorHigh==0x12345600 && vertex.colorLow==0xabcdef00);
    CHECK(NativeMesh_GetQuad(&mesh,0,&quad)==NATIVE_ASSET_OK && quad.flags==0x1000 && quad.drawOrderLow==0x80000000 && quad.drawOrderHigh==0x12345678);
    const u16 expected[2][3]={{2,0,3},{0,1,3}};
    for(unsigned t=0;t<2;t++) {
        CHECK(NativeMesh_GetLowTriangle(&mesh,0,t,&triangle)==NATIVE_ASSET_OK);
        CHECK(memcmp(triangle.indices,expected[t],sizeof(expected[t]))==0);
        for(unsigned k=0;k<3;k++) CHECK(triangle.vertices[k].colorHigh==0x12345600+expected[t][k]);
    }
    CHECK(memcmp(vertexSnapshot,vertexStorage,sizeof(vertexStorage))==0 && memcmp(quadSnapshot,quadStorage,sizeof(quadStorage))==0);
    CHECK(NativeMesh_GetVertex(&mesh,9,&vertex)==NATIVE_ASSET_INDEX_OUT_OF_RANGE && vertex.colorHigh==0);
    CHECK(NativeMesh_GetLowTriangle(&mesh,0,2,&triangle)==NATIVE_ASSET_INDEX_OUT_OF_RANGE && triangle.vertices[0].colorHigh==0);
    CHECK(NativeMesh_GetQuad(&mesh,1,&quad)==NATIVE_ASSET_INDEX_OUT_OF_RANGE && quad.drawOrderLow==0);
    Put16(quads+16,9); // Even the unused midpoint must be valid.
    CHECK(NativeMesh_GetLowTriangle(&mesh,0,0,&triangle)==NATIVE_ASSET_INVALID_DATA && triangle.vertices[1].position[0]==0);
    CHECK(NativeMesh_GetQuad(&mesh,0,&quad)==NATIVE_ASSET_INVALID_DATA && quad.indices[0]==0);
    CHECK(NativeMesh_GetVertex(NULL,0,&vertex)==NATIVE_ASSET_INVALID_ARGUMENT && vertex.flags==0);
    CHECK(NativeMesh_GetQuad(NULL,0,&quad)==NATIVE_ASSET_INVALID_ARGUMENT);
    CHECK(NativeMesh_GetLowTriangle(NULL,0,0,&triangle)==NATIVE_ASSET_INVALID_ARGUMENT);
    CHECK(NativeMesh_GetVertex(&mesh,0,NULL)==NATIVE_ASSET_INVALID_ARGUMENT);
    mesh.vertices=NULL;
    CHECK(NativeMesh_GetVertex(&mesh,0,&vertex)==NATIVE_ASSET_INVALID_ARGUMENT);
    mesh.quads=NULL;
    CHECK(NativeMesh_GetQuad(&mesh,0,&quad)==NATIVE_ASSET_INVALID_ARGUMENT);
    puts("Native mesh geometry OK"); return 0;
}
