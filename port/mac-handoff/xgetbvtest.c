#include <stdio.h>
#include <string.h>
typedef unsigned char BYTE; typedef int BOOL; enum{FALSE,TRUE};
static unsigned long long RIP, RAX, RCX, RDX; static const BYTE *MEM; static unsigned MEMLEN = 3;
static BOOL sequoia_or_later;
#define RIP_sig(x) RIP
#define RAX_sig(x) RAX
#define RCX_sig(x) RCX
#define RDX_sig(x) RDX
#define TRACE_(c) ignore_trace
static void ignore_trace(const char *fmt, ...) { (void)fmt; }
static unsigned int virtual_uninterrupted_read_memory(const BYTE *a, BYTE *b, unsigned n){ (void)a; if (n > MEMLEN) n = MEMLEN; memcpy(b, MEM, n); return n; }
typedef int ucontext_t;
#include "extracted_xgetbv.c"
static int check(const char *n, const BYTE *b, unsigned len, unsigned long long rcx, BOOL seq, int want_ok, unsigned long long want_rax)
{
  MEM=b; MEMLEN=len; RIP=0; RAX=0x1234; RDX=0x5678; RCX=rcx; sequoia_or_later=seq;
  int r = rosetta_emulate_xgetbv(0);
  int good = (RCX==rcx) && (want_ok ? (r && RIP==3 && RAX==want_rax && RDX==0) : (!r && RIP==0 && RAX==0x1234 && RDX==0x5678));
  printf("%-4s %s\n", good?"ok":"FAIL", n); return !good;
}
int main(void){
  BYTE x[]={0x0f,0x01,0xd0}, o[]={0x0f,0x01,0xd1}, u[]={0x0f,0x0b,0x00};
  int f=0;
  f+=check("xgetbv ecx=0, pre-Sequoia -> 0x07", x,3,0,0,1,0x07);
  f+=check("xgetbv ecx=0, Sequoia -> 0xe7", x,3,0,1,1,0xe7);
  f+=check("xgetbv rcx=0xffffffff00000000 (upper ignored) -> ok", x,3,0xffffffff00000000ull,1,1,0xe7);
  f+=check("xgetbv ecx=1 -> not handled", x,3,1,1,0,0);
  f+=check("xsetbv 0F 01 D1 -> not handled", o,3,0,1,0,0);
  f+=check("ud2 -> not handled", u,3,0,1,0,0);
  f+=check("short read (2 bytes) -> not handled", x,2,0,1,0,0);
  f+=check("empty read -> not handled", x,0,0,1,0,0);
  f+=check("one-byte read -> not handled", x,1,0,1,0,0);
  BYTE prefixed[]={0x66,0x0f,0x01,0xd0};
  f+=check("prefix is not emulated", prefixed,4,0,1,0,0);
  f+=check("ecx=0xffffffff -> not handled", x,3,0xffffffffull,1,0,0);
  printf("XGETBV checks: 11, failures: %d\n",f);
  return f;
}
