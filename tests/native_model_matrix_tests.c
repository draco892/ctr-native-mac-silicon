#include <platform/native_model_matrix.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while (0)
static const struct NativeModelMatrix identity={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
static long long FloorQ12(long long n) { return n<0 ? -((-n+4095)/4096) : n/4096; }
static s16 Clamp(long long n) { return (s16)(n < -32768 ? -32768 : n > 32767 ? 32767 : n); }
static int TestGolden(void)
{
	struct NativeModelMatrix matrix, composed;
	const s16 one[]={4096,4096,4096};
	CHECK(NativeModelMatrix_Build(&identity,one,one,4095,0,&matrix)==NATIVE_ASSET_OK && memcmp(&matrix,&identity,sizeof(matrix))==0);
	CHECK(NativeModelMatrix_Build(&identity,one,one,4096,0,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==1024 && matrix.m[1][1]==1024 && matrix.m[2][2]==1024);
	CHECK(NativeModelMatrix_Build(&identity,one,one,4096,1,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==1536);
	CHECK(NativeModelMatrix_Build(&identity,one,one,4095,1,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==6143);
	const struct NativeModelMatrix turn={.m={{0,-4096,0},{4096,0,0},{0,0,4096}}};
	const s16 unequal[]={4096,8192,2048};
	CHECK(NativeModelMatrix_Build(&turn,unequal,one,0,0,&matrix)==NATIVE_ASSET_OK);
	CHECK(matrix.m[0][1]==-8192 && matrix.m[1][0]==4096 && matrix.m[2][2]==2048);
	CHECK(NativeModelMatrix_Compose(&turn,&matrix,&composed)==NATIVE_ASSET_OK && composed.m[0][0]==-4096 && composed.m[1][1]==-8192);
	struct NativePackedModelVertex vertex={.xy=0x00020001,.z=0xfffffffdu}; struct NativeMatrixVector out;
	CHECK(NativeModelMatrix_Apply(&identity,&vertex,&out)==NATIVE_ASSET_OK && out.mac[0]==1 && out.mac[1]==2 && out.mac[2]==-3 && out.saturated==0);
	matrix=(struct NativeModelMatrix){.m={{-1,0,0},{0,32767,0},{0,0,-32768}}};
	vertex=(struct NativePackedModelVertex){.xy=0x7fff0001,.z=0xffff8000};
	CHECK(NativeModelMatrix_Apply(&matrix,&vertex,&out)==NATIVE_ASSET_OK && out.mac[0]==-1 && out.ir[0]==-1 && out.ir[1]==32767 && out.ir[2]==32767 && out.saturated==6);
	const s16 negative[]={-4096,-32768,32767};
	CHECK(NativeModelMatrix_Build(&identity,negative,one,4096,0,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==15360 && matrix.m[1][1]==8192 && matrix.m[2][2]==8191);
	CHECK(NativeModelMatrix_Build(&identity,one,one,INT32_MIN,0,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==1024);
	CHECK(NativeModelMatrix_Build(&identity,one,one,INT32_MAX,1,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==1023);
	CHECK(NativeModelMatrix_Build(&identity,one,one,-1,1,&matrix)==NATIVE_ASSET_OK && matrix.m[0][0]==4095);
	CHECK(NativeModelMatrix_Compose(NULL,&identity,&composed)==NATIVE_ASSET_INVALID_ARGUMENT && composed.m[0][0]==0);
	CHECK(NativeModelMatrix_Build(&identity,one,one,0,2,&matrix)==NATIVE_ASSET_INVALID_ARGUMENT && matrix.m[0][0]==0);
	CHECK(NativeModelMatrix_Apply(NULL,&vertex,&out)==NATIVE_ASSET_INVALID_ARGUMENT && out.mac[0]==0 && out.saturated==0);
	return 0;
}
static s16 Random16(u32 *state)
{
	*state=*state*1664525u+1013904223u; return (s16)((int)(*state%65536)-32768);
}
static int TestDifferential(void)
{
	u32 random=0x3c6ef372;
	for(unsigned trial=0;trial<10000;trial++)
	{
		struct NativeModelMatrix a,b,out; struct NativeMatrixVector vector;
		s16 model[3],instance[3],position[3];
		for(unsigned r=0;r<3;r++) for(unsigned c=0;c<3;c++) { a.m[r][c]=Random16(&random); b.m[r][c]=Random16(&random); }
		struct NativeModelMatrix snapshotA=a,snapshotB=b;
		CHECK(NativeModelMatrix_Compose(&a,&b,&out)==NATIVE_ASSET_OK);
		for(unsigned r=0;r<3;r++) for(unsigned c=0;c<3;c++)
		{
			long long sum=(long long)a.m[r][0]*b.m[0][c]+(long long)a.m[r][1]*b.m[1][c]+(long long)a.m[r][2]*b.m[2][c];
			CHECK(out.m[r][c]==Clamp(FloorQ12(sum)));
		}
		for(unsigned i=0;i<3;i++) { model[i]=Random16(&random); instance[i]=Random16(&random); position[i]=Random16(&random); }
		struct NativePackedModelVertex packed={.xy=(u16)position[0]+((u32)(u16)position[1]*65536u),.z=(u16)position[2]};
		CHECK(NativeModelMatrix_Apply(&a,&packed,&vector)==NATIVE_ASSET_OK);
		unsigned expectedMask=0;
		for(unsigned r=0;r<3;r++)
		{
			long long sum=(long long)a.m[r][0]*position[0]+(long long)a.m[r][1]*position[1]+(long long)a.m[r][2]*position[2];
			long long shifted=FloorQ12(sum);
			CHECK(vector.mac[r]==shifted && vector.ir[r]==Clamp(shifted));
			if(shifted!=Clamp(shifted)) expectedMask+=1u<<r;
		}
		CHECK(vector.saturated==expectedMask);
		int depth=(int)trial-5000, pixel=trial%2;
		CHECK(NativeModelMatrix_Build(&a,model,instance,depth,pixel,&out)==NATIVE_ASSET_OK);
		for(unsigned c=0;c<3;c++)
		{
			long long scale=instance[c];
			if(pixel)
			{
				long long half=depth<0 ? -((-(long long)depth+1)/2) : depth/2;
				long long product=(half+4096)*scale;
				unsigned long long modulo=(unsigned long long)product%4294967296ull;
				long long signedProduct=modulo>2147483647ull ? (long long)modulo-4294967296LL : (long long)modulo;
				long long scaled=FloorQ12(signedProduct);
				unsigned low=(unsigned long long)scaled%65536ull;
				scale=low>32767 ? (long long)low-65536 : low;
			}
			unsigned coefficient=(u16)model[c]; if(depth>=4096) coefficient/=4;
			long long signedCoefficient=coefficient>32767 ? (long long)coefficient-65536 : coefficient;
			long long diagonal=Clamp(FloorQ12(signedCoefficient*scale));
			for(unsigned r=0;r<3;r++) CHECK(out.m[r][c]==Clamp(FloorQ12(a.m[r][c]*diagonal)));
		}
		CHECK(memcmp(&a,&snapshotA,sizeof(a))==0 && memcmp(&b,&snapshotB,sizeof(b))==0);
	}
	return 0;
}
int main(void)
{
	if(TestGolden() || TestDifferential()) return 1;
	puts("Native model matrices: Q12 scale/composition, depth threshold, pixel LOD, saturation and 10000 differential cases passed."); return 0;
}
