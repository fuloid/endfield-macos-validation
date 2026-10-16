#include <stdio.h>
#include <string.h>
typedef unsigned char BYTE; typedef int BOOL; enum{FALSE,TRUE};
static unsigned long long RIP; static const BYTE *MEM; static unsigned MEMLEN = 16;
#define RIP_sig(x) RIP
#define TRACE_(c) ignore_trace
static void ignore_trace(const char *fmt, ...) { (void)fmt; }
static unsigned int virtual_uninterrupted_read_memory(const BYTE *a, BYTE *b, unsigned n){ (void)a; if(n>MEMLEN) n=MEMLEN; memcpy(b, MEM, n); return n; }
typedef int ucontext_t;
#include "extracted.c"
static int check(const BYTE *bytes, unsigned len, unsigned want)
{
    MEM=bytes; MEMLEN=len; RIP=0x1000;
    int handled=rosetta_skip_long_nop(0);
    return handled != (want != 0) || RIP != 0x1000 + want;
}
int main(void){
  struct { BYTE b[16]; unsigned want; const char *n; } t[] = {
    {{0x0f,0x1f,0xc1},3,"nop ecx (reg form, the Endfield one)"},
    {{0x0f,0x1f,0x00},3,"nop [rax]"},
    {{0x0f,0x1f,0x40,0},4,"nop [rax+0]"},
    {{0x0f,0x1f,0x44,0,0},5,"nop [rax+rax+0]"},
    {{0x66,0x0f,0x1f,0x44,0,0},6,"66 nop [rax+rax+0]"},
    {{0x0f,0x1f,0x80,0,0,0,0},7,"nop [rax+disp32]"},
    {{0x0f,0x1f,0x84,0,0,0,0,0},8,"nop [rax+rax+disp32]"},
    {{0x66,0x2e,0x0f,0x1f,0x84,0,0,0,0,0},10,"66 2E nop (cs-prefixed, gcc padding)"},
    {{0x0f,0x1f,0x05,1,2,3,4},7,"nop [rip+disp32]"},
    {{0x0f,0x1f,0x04,0x25,1,2,3,4},8,"nop [disp32] via SIB"},
    {{0x0f,0x0b},0,"ud2 (must NOT skip)"},
  };
  int fail=0;
  unsigned count=0;
  for (unsigned i=0;i<sizeof t/sizeof *t;i++){
    int bad=check(t[i].b,16,t[i].want);
    printf("%-4s %-40s\n",bad?"FAIL":"ok",t[i].n); fail+=bad; count++;
  }
  /* Every ModRM, including SIB/no-base, and every shorter readable length.
     A rejected/truncated instruction must leave RIP unchanged. */
  for(unsigned modrm=0;modrm<256;modrm++){
    unsigned mod=modrm>>6,rm=modrm&7;
    BYTE b[16]={0x0f,0x1f,(BYTE)modrm,0x25,0,0,0,0,0};
    unsigned want=3;
    if(mod!=3){
      if(rm==4) { want++; if(mod==0) want+=4; }
      else if(mod==0 && rm==5) want+=4;
      if(mod==1) want++; else if(mod==2) want+=4;
    }
    if((modrm>>3)&7) want=0;
    for(unsigned len=0;len<=16;len++){
      int bad=check(b,len,len>=want?want:0);
      if(bad) printf("FAIL ModRM=%02x readable=%u\n",modrm,len);
      fail+=bad; count++;
    }
  }
  {
    BYTE prefixes[]={0x26,0x2e,0x36,0x3e,0x64,0x65,0x66,0x67,0xf2,0xf3,0x48};
    for(unsigned i=0;i<sizeof prefixes;i++){
      BYTE b[16]={prefixes[i],0x0f,0x1f,0xc1};
      fail+=check(b,4,4); count++;
    }
    BYTE b[16]; memset(b,0x66,sizeof b);
    b[12]=0x0f; b[13]=0x1f; b[14]=0xc1;
    fail+=check(b,15,15); count++;
    memset(b,0x66,sizeof b); b[13]=0x0f; b[14]=0x1f; b[15]=0xc1;
    fail+=check(b,16,0); count++;
    memset(b,0x66,sizeof b); fail+=check(b,16,0); count++;
    BYTE lock[]={0xf0,0x0f,0x1f,0xc1};
    fail+=check(lock,4,0); count++;
  }
  printf("NOP checks: %u, failures: %d\n",count,fail);
  return fail;
}
