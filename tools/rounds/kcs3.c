// kcs3: dictionary brute force of the kCFI hash _ZTSF<ret>P11task_struct<P|PK><len><name>E, name = 1..3 words of a list in 5 spellings (+ optional _t)
// usage: kcs3 want_hex wordfile ret(v) part nparts maxwords      (part/nparts splits the first word)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
typedef uint64_t u64; typedef uint32_t u32;
#define P1 0x9E3779B185EBCA87ULL
#define P2 0xC2B2AE3D27D4EB4FULL
#define P3 0x165667B19E3779F9ULL
#define P4 0x85EBCA77C2B2AE63ULL
#define P5 0x27D4EB2F165667C5ULL
static inline u64 rotl(u64 x,int r){return (x<<r)|(x>>(64-r));}
static inline u64 rnd(u64 a,u64 i){a+=i*P2;a=rotl(a,31);return a*P1;}
static inline u64 mrg(u64 a,u64 v){v=rnd(0,v);a^=v;return a*P1+P4;}
static inline u64 rd64(const unsigned char*p){u64 v;memcpy(&v,p,8);return v;}
static inline u32 rd32(const unsigned char*p){u32 v;memcpy(&v,p,4);return v;}
static u64 xxh64(const unsigned char*b,size_t n){
  size_t p=0;u64 h;
  if(n>=32){u64 v[4]={P1+P2,P2,0,(u64)0-P1};
    while(p+32<=n){for(int i=0;i<4;i++){v[i]=rnd(v[i],rd64(b+p));p+=8;}}
    h=rotl(v[0],1)+rotl(v[1],7)+rotl(v[2],12)+rotl(v[3],18);
    for(int i=0;i<4;i++)h=mrg(h,v[i]);
  } else h=P5;
  h+=n;
  while(p+8<=n){u64 k=rnd(0,rd64(b+p));h^=k;h=rotl(h,27)*P1+P4;p+=8;}
  if(p+4<=n){h^=(u64)rd32(b+p)*P1;h=rotl(h,23)*P2+P3;p+=4;}
  while(p<n){h^=b[p]*P5;h=rotl(h,11)*P1;p++;}
  h^=h>>33;h*=P2;h^=h>>29;h*=P3;h^=h>>32;return h;}
static u32 want; static char **W; static int nW; static char retc[8];
static char pre[64]; static int prelen;
static void test(const char *name, int nl, int pk){
  char buf[256]; int l=0;
  memcpy(buf,pre,prelen); l=prelen;
  buf[l++]='P'; if(pk) buf[l++]='K';
  l+=sprintf(buf+l,"%d",nl); memcpy(buf+l,name,nl); l+=nl; buf[l++]='E';
  if((u32)xxh64((unsigned char*)buf,l)==want){buf[l]=0;printf("HIT %s\n",buf);fflush(stdout);}
}
static void tryname(const char *name){
  int nl=strlen(name); char t[200];
  test(name,nl,0); test(name,nl,1);
  memcpy(t,name,nl); t[nl]='_'; t[nl+1]='t'; t[nl+2]=0; test(t,nl+2,0); test(t,nl+2,1);
}
int main(int argc,char**argv){
  want=(u32)strtoul(argv[1],0,16); FILE*f=fopen(argv[2],"r"); strcpy(retc,argv[3]); int part=atoi(argv[4]),np=atoi(argv[5]); int maxw=atoi(argv[6]);
  W=malloc(8*200000); char b[128]; while(fgets(b,128,f)){b[strcspn(b,"\r\n")]=0; if(b[0]) W[nW++]=strdup(b);}
  prelen=sprintf(pre,"_ZTSF%sP11task_struct",retc);
  char name[256], cap[200][64];
  // capitalised copies
  char **C=malloc(8*nW); for(int i=0;i<nW;i++){C[i]=strdup(W[i]); C[i][0]=toupper(C[i][0]);}
  for(int a=part;a<nW;a+=np){
    for(int v=0;v<5;v++){ // single word spellings
      if(v==0) tryname(W[a]); else if(v==1) tryname(C[a]);
    }
    if(maxw<2) continue;
    for(int b2=0;b2<nW;b2++){
      const char*s1[2]={W[a],C[a]}; const char*s2[2]={W[b2],C[b2]};
      for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++){
        snprintf(name,256,"%s_%s",s1[c1],s2[c2]); tryname(name);
        snprintf(name,256,"%s%s",s1[c1],s2[c2]); tryname(name);
      }
      if(maxw<3) continue;
      for(int c3=0;c3<nW;c3++){
        const char*s3[2]={W[c3],C[c3]};
        for(int x=0;x<2;x++) for(int y=0;y<2;y++) for(int z=0;z<2;z++){
          snprintf(name,256,"%s_%s_%s",s1[x],s2[y],s3[z]); tryname(name);
          snprintf(name,256,"%s%s%s",s1[x],s2[y],s3[z]); tryname(name);
        }
      }
    }
  }
  printf("done part %d\n",part); return 0;
}
