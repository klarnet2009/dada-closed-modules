// kcs: kCFI prototype brute force with Itanium substitutions. usage: kcs N want_hex[,want_hex] type1 type2 ...  (types like m, Pv, P11task_struct, PK11task_struct ; v=void allowed as ret only)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint64_t u64; typedef uint32_t u32;
#define P1 0x9E3779B185EBCA87ULL
#define P2 0xC2B2AE3D27D4EB4FULL
#define P3 0x165667B19E3779F9ULL
#define P4 0x85EBCA77C2B2AE63ULL
#define P5 0x27D4EB2F165667C5ULL
static inline u64 rotl(u64 x,int r){return (x<<r)|(x>>(64-r));}
static inline u64 rnd(u64 a,u64 i){a+=i*P2;a=rotl(a,31);return a*P1;}
static inline u64 mrg(u64 a,u64 v){v=rnd(0,v);a^=v;return a*P1+P4;}
static u64 rd64(const unsigned char*p){u64 v;memcpy(&v,p,8);return v;}
static u32 rd32(const unsigned char*p){u32 v;memcpy(&v,p,4);return v;}
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
// mangler
static const char* tab[64]; static int ntab;
static char out[512]; static int ol;
static int intab(const char*s){for(int i=0;i<ntab;i++) if(!strcmp(tab[i],s)) return i; return -1;}
static const char* enc(const char*s){ // returns pointer to rest after this type (s is a suffix string; type = whole remaining? no: types are standalone strings)
  return s;}
static void emit_sub(int i){out[ol++]='S'; if(i>0){int v=i-1; char d[8];int n=0; do{int r=v%36; d[n++]=r<10?'0'+r:'A'+r-10; v/=36;}while(v); while(n)out[ol++]=d[--n];} out[ol++]='_';}
static void mangle(const char*s){ // s is a full standalone type string
  int i=intab(s); if(i>=0){emit_sub(i);return;}
  if(s[0]=='P'||s[0]=='K'){out[ol++]=s[0];mangle(s+1);tab[ntab++]=s;return;}
  if(s[0]>='0'&&s[0]<='9'){int l=atoi(s);int k=0;while(s[k]>='0'&&s[k]<='9')k++;memcpy(out+ol,s,k+l);ol+=k+l;tab[ntab++]=s;return;}
  out[ol++]=s[0]; // builtin
}
static u32 want[16]; static int nw;
static char **L[16]; static int Ln[16]; static int nslots;
static const char* sel[16]; static const char* ret;
static char **R; static int Rn;
static int lm(char*** o,const char*spec){ if(spec[0]!='@'){char**a=malloc(8);a[0]=strdup(spec);*o=a;return 1;}
  FILE*f=fopen(spec+1,"r");char**a=malloc(8*1000000);int n=0;char b[256];while(fgets(b,256,f)){b[strcspn(b,"\n")]=0;if(b[0])a[n++]=strdup(b);}*o=a;return n;}
static void check(int k){
  ntab=0;ol=0;memcpy(out,"_ZTSF",5);ol=5;
  mangle(ret);
  if(k==0){out[ol++]='v';}
  for(int i=0;i<k;i++)mangle(sel[i]);
  out[ol++]='E';out[ol]=0;
  u32 h=(u32)xxh64((unsigned char*)out,ol);
  for(int i=0;i<nw;i++)if(h==want[i])printf("HIT %#x %s\n",h,out);
}
static void rec(int d){ if(d==nslots){check(nslots);return;} for(int i=0;i<Ln[d];i++){sel[d]=L[d][i];rec(d+1);} }
int main(int argc,char**argv){
  // kcs2 want ret_spec arg_spec... ; spec = literal type or @file (one type per line)
  char*w=strtok(argv[1],","); while(w){want[nw++]=(u32)strtoul(w,0,16);w=strtok(0,",");}
  Rn=lm(&R,argv[2]);
  for(int i=3;i<argc;i++){Ln[nslots]=lm(&L[nslots],argv[i]);nslots++;}
  for(int r=0;r<Rn;r++){ret=R[r]; for(int k=nslots;k<=nslots;k++)rec(0);}
  printf("done\n");return 0;}
