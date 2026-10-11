#include <platform/native_model_projection.h>
#include <psx/gtereg.h>
#include <stdio.h>
#include <string.h>
extern int GTE_operator(int op);
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"failed: %s at line %d\n",#c,__LINE__); return 1; } } while (0)
static const struct NativeModelMatrix identity={.m={{4096,0,0},{0,4096,0},{0,0,4096}}};
static void ReferenceLoad(const struct NativeProjectionConfig *c,const struct NativePackedModelVertex *v,
    const struct NativeProjectionState *state)
{
	memset(&gteRegs,0,sizeof(gteRegs));
	for(unsigned i=0;i<9;i++)
	{
		unsigned reg=i/2; s16 coefficient=c->rotation.m[i/3][i%3];
		if(i%2) gteRegs.CP2C.p[reg].sw.h=coefficient; else gteRegs.CP2C.p[reg].sw.l=coefficient;
	}
	for(unsigned i=0;i<3;i++)
	{
		gteRegs.CP2C.p[5+i].sd=c->translation[i];
		gteRegs.CP2D.p[i*2].d=v[i].xy; gteRegs.CP2D.p[i*2+1].d=v[i].z;
		gteRegs.CP2D.p[12+i].sw.l=state->screen[i][0]; gteRegs.CP2D.p[12+i].sw.h=state->screen[i][1];
	}
	for(unsigned i=0;i<4;i++) gteRegs.CP2D.p[16+i].w.l=state->depth[i];
	C2_OFX=c->offset[0]; C2_OFY=c->offset[1]; gteRegs.CP2C.p[26].w.l=c->h;
	C2_DQA=c->dqa; C2_DQB=c->dqb;
}
static int TestGolden(void)
{
	struct NativeProjectionConfig config={.rotation=identity,.translation={0,0,1024},.offset={160*65536,120*65536},.h=256};
	struct NativePackedModelVertex v[3]={{.xy=0x00500040},{.xy=0xffb0ffc0},{.xy=0}};
	struct NativeProjectionState state={0}; struct NativeProjectionResult out;
	CHECK(NativeModelProjection_Project(&config,v,&state,&out)==NATIVE_ASSET_OK);
	CHECK(out.ratio==16384 && out.mac[2]==1024 && state.depth[3]==1024 && state.screen[2][0]==176 && state.screen[2][1]==140 && out.flags==0);
	CHECK(NativeModelProjection_Project3(&config,v,&state,&out)==NATIVE_ASSET_OK);
	CHECK(state.screen[0][0]==176 && state.screen[1][0]==144 && state.screen[2][0]==160 && state.depth[0]==1024 && state.depth[3]==1024);
	config.translation[2]=-1;
	CHECK(NativeModelProjection_Project(&config,v,&state,&out)==NATIVE_ASSET_OK && state.depth[3]==0 && out.ratio==131071 && (out.flags&0x80060000u)==0x80060000u);
	config.translation[2]=128;
	CHECK(NativeModelProjection_Project(&config,v,&state,&out)==NATIVE_ASSET_OK && out.ratio==131071 && (out.flags&0x20000u));
	config.translation[2]=129;
	CHECK(NativeModelProjection_Project(&config,v,&state,&out)==NATIVE_ASSET_OK && !(out.flags&0x20000u));
	config.translation[0]=32767; config.translation[1]=-32768; config.translation[2]=65535; config.h=65535;
	CHECK(NativeModelProjection_Project(&config,v,&state,&out)==NATIVE_ASSET_OK && state.screen[2][0]==1023 && state.screen[2][1]==-1024 && (out.flags&0x6000u)==0x6000u);
	struct NativeProjectionState before=state;
	CHECK(NativeModelProjection_Project(NULL,v,&state,&out)==NATIVE_ASSET_INVALID_ARGUMENT && out.flags==0 && memcmp(&before,&state,sizeof(state))==0);
	return 0;
}
static int TestViewTranslation(void)
{
	s32 instance[3]={20,-30,1000},camera[3]={10,-10,100},translation[3],depth;
	CHECK(NativeModelProjection_ViewTranslation(&identity,instance,camera,0,0,translation,&depth)==NATIVE_ASSET_OK && depth==900 && translation[0]==40 && translation[1]==-80 && translation[2]==3600);
	CHECK(NativeModelProjection_ViewTranslation(&identity,instance,camera,0,1,translation,&depth)==NATIVE_ASSET_OK && translation[0]==10 && translation[2]==900);
	instance[2]=5000;
	CHECK(NativeModelProjection_ViewTranslation(&identity,instance,camera,0,0,translation,&depth)==NATIVE_ASSET_OK && depth==4900 && translation[2]==4900);
	instance[0]=65535; camera[0]=0;
	CHECK(NativeModelProjection_ViewTranslation(&identity,instance,camera,0,0,translation,&depth)==NATIVE_ASSET_OK && translation[0]==-1);
	instance[0]=INT32_MAX; camera[0]=INT32_MIN;
	CHECK(NativeModelProjection_ViewTranslation(&identity,instance,camera,0,0,translation,&depth)==NATIVE_ASSET_OK && translation[0]==-1);
	instance[0]=-5; instance[2]=INT32_MIN;
	CHECK(NativeModelProjection_ViewTranslation(NULL,instance,NULL,1,1,translation,&depth)==NATIVE_ASSET_OK && depth==INT32_MIN && translation[0]==-2 && translation[2]==-536870912);
	CHECK(NativeModelProjection_ViewTranslation(NULL,instance,camera,0,0,translation,&depth)==NATIVE_ASSET_INVALID_ARGUMENT && depth==0 && translation[0]==0);
	return 0;
}
static u32 Random(u32 *state) { *state=*state*1664525u+1013904223u; return *state; }
static s32 Signed32(u32 n) { return n<=INT32_MAX ? (s32)n : (s32)((s64)n-4294967296LL); }
static s16 Signed16(u32 n) { n&=65535; return (s16)(n<32768 ? (int)n : (int)n-65536); }
static int TestCoreDifferential(void)
{
	u32 random=0xa54ff53a;
	for(unsigned trial=0;trial<10000;trial++)
	{
		struct NativeProjectionConfig c={0}; struct NativePackedModelVertex v[3];
		struct NativeProjectionState state={0}; struct NativeProjectionResult out;
		for(unsigned r=0;r<3;r++)
		{
			for(unsigned col=0;col<3;col++) c.rotation.m[r][col]=Signed16(Random(&random));
			c.translation[r]=trial%5==0 ? Signed32(Random(&random)) : (int)(Random(&random)%131072)-65536;
			v[r].xy=Random(&random); v[r].z=Random(&random);
			for(unsigned k=0;k<2;k++) state.screen[r][k]=Signed16(Random(&random));
		}
		for(unsigned i=0;i<4;i++) state.depth[i]=(u16)Random(&random);
		c.offset[0]=Signed32(Random(&random)); c.offset[1]=Signed32(Random(&random)); c.h=(u16)Random(&random);
		c.dqa=trial%3==0 ? 1 : Signed16(Random(&random)); c.dqb=trial%3==0 ? 0 : Signed32(Random(&random));
		ReferenceLoad(&c,v,&state);
		CHECK(GTE_operator(trial%2 ? 0x80030 : 0x80001)==1);
		GTERegisters reference=gteRegs;
		CHECK((trial%2 ? NativeModelProjection_Project3(&c,v,&state,&out) : NativeModelProjection_Project(&c,v,&state,&out))==NATIVE_ASSET_OK);
		CHECK(memcmp(&reference,&gteRegs,sizeof(reference))==0); // New module must never touch global GTE.
		CHECK(out.flags==C2_FLAG && out.mac0==C2_MAC0 && out.ir0==C2_IR0);
		for(unsigned r=0;r<3;r++)
		{
			CHECK(out.mac[r]==gteRegs.CP2D.p[25+r].sd && out.ir[r]==gteRegs.CP2D.p[9+r].sw.l);
			CHECK(state.screen[r][0]==gteRegs.CP2D.p[12+r].sw.l && state.screen[r][1]==gteRegs.CP2D.p[12+r].sw.h);
		}
		for(unsigned i=0;i<4;i++) CHECK(state.depth[i]==gteRegs.CP2D.p[16+i].w.l);
		if(trial%3==0) CHECK(out.ratio==(u32)out.mac0);
	}
	return 0;
}
static int TestTranslationDifferential(void)
{
	u32 random=0x510e527f;
	for(unsigned trial=0;trial<10000;trial++)
	{
		struct NativeModelMatrix view;
		s32 instance[3],camera[3],out[3],depth,expected[3];
		memset(&gteRegs,0,sizeof(gteRegs));
		for(unsigned i=0;i<9;i++)
		{
			s16 coefficient=Signed16(Random(&random)); view.m[i/3][i%3]=coefficient;
			if(i%2) gteRegs.CP2C.p[8+i/2].sw.h=coefficient; else gteRegs.CP2C.p[8+i/2].sw.l=coefficient;
		}
		int screenspace=trial%2,huge=(trial/2)%2;
		for(unsigned i=0;i<3;i++)
		{
			instance[i]=Signed32(Random(&random)); camera[i]=Signed32(Random(&random));
			gteRegs.CP2D.p[9+i].sw.l=Signed16((u32)instance[i]-(u32)camera[i]);
		}
		if(!screenspace) CHECK(GTE_operator(0x04be012)==1);
		for(unsigned i=0;i<3;i++) expected[i]=screenspace ? instance[i] : gteRegs.CP2D.p[9+i].sw.l;
		s32 expectedDepth=expected[2];
		if(Signed32((u32)expectedDepth-4096u)<0)
			for(unsigned i=0;i<3;i++) expected[i]=Signed32((u32)expected[i]*4u);
		if(huge) for(unsigned i=0;i<3;i++)
			expected[i]=expected[i]<0 ? (s32)(-((-(s64)expected[i]+3)/4)) : expected[i]/4;
		GTERegisters snapshot=gteRegs;
		CHECK(NativeModelProjection_ViewTranslation(&view,instance,camera,screenspace,huge,out,&depth)==NATIVE_ASSET_OK);
		CHECK(depth==expectedDepth && memcmp(out,expected,sizeof(out))==0 && memcmp(&snapshot,&gteRegs,sizeof(snapshot))==0);
	}
	return 0;
}
int main(void)
{
	if(TestGolden() || TestViewTranslation() || TestCoreDifferential() || TestTranslationDifferential()) return 1;
	puts("Native projection: translation, RTPS/RTPT, FIFO, divisor/flags and 10000 projection plus 10000 translation native GTE comparisons passed."); return 0;
}
