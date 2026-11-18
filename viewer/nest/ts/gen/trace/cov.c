/* coverage tracer: int3 at listed addresses, counts hits (re-arm via single-step) */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ucontext.h>
#include <sys/mman.h>
#include "pe.h"
static uint64_t *addrs; static uint8_t *orig; static uint64_t *cnt; static int n;
static long rearm = -1; static int oneshot; static FILE *tracef; static long ntrace; static FILE *icallf; static uint64_t dumpf; static FILE *dumpo;
static int find(uint64_t a){int lo=0,hi=n-1;while(lo<=hi){int m=(lo+hi)/2;if(addrs[m]==a)return m;if(addrs[m]<a)lo=m+1;else hi=m-1;}return -1;}
static void h(int sig, siginfo_t *si, void *uc_){ (void)sig;(void)si;
  ucontext_t *uc=uc_; greg_t *g=uc->uc_mcontext.gregs;
  if (rearm>=0) { if(icallf) fprintf(icallf,"%lx %lx\n",(unsigned long)addrs[rearm],(unsigned long)(g[REG_RIP]-(uint64_t)g_img.base+g_img.pref_base)); *(uint8_t*)(addrs[rearm]-g_img.pref_base+(uint64_t)g_img.base)=0xcc; rearm=-1; g[REG_EFL]&=~0x100;
    return; }
  uint64_t rip=g[REG_RIP]-1; int k=find(rip-(uint64_t)g_img.base+g_img.pref_base);
  if(k<0){fprintf(stderr,"cov: stray trap at %lx\n",(unsigned long)rip);abort();}
  if(dumpo && addrs[k]==dumpf && getenv("COV_DUMPG")){ unsigned long ga=strtoul(getenv("COV_DUMPG"),0,16); fprintf(dumpo,"%x %lx | %lx %lx\n",*(unsigned*)ga,*(unsigned long*)ga,(unsigned long)g[REG_RCX],(unsigned long)g[REG_RDX]); } else if(dumpo && addrs[k]==dumpf && getenv("COV_DUMPX")){ double x[4]; for(int q=0;q<4;q++) memcpy(&x[q], &uc->uc_mcontext.fpregs->_xmm[q], 8); fprintf(dumpo,"%.17g %.17g %.17g %.17g | %lx %lx\n",x[0],x[1],x[2],x[3],(unsigned long)g[REG_RCX],(unsigned long)g[REG_RDX]); } else if(dumpo && addrs[k]==dumpf){ long off=getenv("COV_DUMPO")?strtol(getenv("COV_DUMPO"),0,16):0; double *a=(double*)(g[REG_RCX]+off), *b=(double*)g[REG_RDX]; fprintf(dumpo,"%.17g %.17g %.17g %.17g %.17g | %.17g %.17g %.17g %.17g %.17g\n",a[0],a[1],a[2],a[3],a[4],b[0],b[1],b[2],b[3],b[4]); } cnt[k]++; if(tracef && ntrace < 20000000){ ntrace++; fprintf(tracef,"%lx %lx %lx %lx %lx\n",(unsigned long)addrs[k],(unsigned long)g[REG_RCX],(unsigned long)g[REG_RDX],(unsigned long)g[REG_R8],(unsigned long)g[REG_R9]); } *(uint8_t*)rip=orig[k]; g[REG_RIP]=rip; if(!oneshot){ g[REG_EFL]|=0x100; rearm=k; }
}
static int cmpu(const void*a,const void*b){uint64_t x=*(uint64_t*)a,y=*(uint64_t*)b;return x<y?-1:x>y;}
void cov_arm(void){ oneshot=getenv("COV_ONESHOT")!=0; if(getenv("COV_TRACE")) tracef=fopen(getenv("COV_TRACE"),"w"); if(getenv("COV_ICALL")) icallf=fopen(getenv("COV_ICALL"),"w"); if(getenv("COV_DUMPF")){ dumpf=strtoull(getenv("COV_DUMPF"),0,16); dumpo=fopen("dump.txt","w"); } const char *f=getenv("COV_ADDRS"); if(!f)return; FILE*fp=fopen(f,"r"); if(!fp)return;
  int cap=1<<22; addrs=malloc(cap*8); char l[256]; while(fgets(l,sizeof l,fp)){ uint64_t a=strtoull(l,0,16); if(a) addrs[n++]=a; }
  fclose(fp); qsort(addrs,n,8,cmpu); orig=malloc(n); cnt=calloc(n,8);
  mprotect(g_img.base, g_img.size, PROT_READ|PROT_WRITE|PROT_EXEC);
  for(int i=0;i<n;i++){ uint8_t*p=(uint8_t*)(addrs[i]-g_img.pref_base+(uint64_t)g_img.base); orig[i]=*p; *p=0xcc; }
  struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=h; sa.sa_flags=SA_SIGINFO|SA_NODEFER; sigaction(SIGTRAP,&sa,0);
}
void cov_dump(void){ if(dumpo) fclose(dumpo); if(tracef) fclose(tracef); if(icallf) fclose(icallf); const char *f=getenv("COV_OUT"); if(!f||!n)return; FILE*fp=fopen(f,"w");
  for(int i=0;i<n;i++) if(cnt[i]) fprintf(fp,"%lx %lu\n",(unsigned long)addrs[i],(unsigned long)cnt[i]); fclose(fp);}
