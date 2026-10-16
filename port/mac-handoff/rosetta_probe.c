/* Exercise the real x86_64 Wine/Rosetta signal path without loading the game.
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror rosetta_probe.c -o rosetta_probe.exe */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cpuid.h>

static BYTE *code;
static DWORD expected_exception;
static unsigned skip, seen, unexpected;

static LONG CALLBACK handle_exception(EXCEPTION_POINTERS *ep)
{
    if ((uintptr_t)ep->ExceptionRecord->ExceptionAddress != (uintptr_t)code)
        return EXCEPTION_CONTINUE_SEARCH;
    seen++;
    if (ep->ExceptionRecord->ExceptionCode != expected_exception)
    {
        printf("FAIL exception: expected %08lx, got %08lx\n", expected_exception,
               ep->ExceptionRecord->ExceptionCode);
        unexpected++;
    }
    ep->ContextRecord->Rip += skip;
    ep->ContextRecord->Rax = 0;
    ep->ContextRecord->Rdx = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void load_code(const BYTE *bytes, unsigned len)
{
    memcpy(code, bytes, len);
    FlushInstructionCache(GetCurrentProcess(), code, len);
    seen=unexpected=0;
}

int main(void)
{
    int failures=0;
    unsigned a,b,c,d;
    if (__get_cpuid(1,&a,&b,&c,&d))
        printf("CPUID: OSXSAVE=%u AVX=%u",(c>>27)&1,(c>>28)&1);
    if (__get_cpuid_count(7,0,&a,&b,&c,&d))
        printf(" AVX2=%u\n",(b>>5)&1);
    PVOID handler;
    code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if (!code) return 2;
    handler=AddVectoredExceptionHandler(1,handle_exception);
    if (!handler) { VirtualFree(code,0,MEM_RELEASE); return 2; }
    typedef uint64_t (*call_fn)(unsigned);
    call_fn call=(call_fn)(uintptr_t)code;
    const BYTE nops[][10]={
        {0x0f,0x1f,0xc1}, {0x0f,0x1f,0x00},
        {0x66,0x2e,0x0f,0x1f,0x84,0,0,0,0,0}
    };
    const unsigned lengths[]={3,3,10};
    for(unsigned i=0;i<3;i++)
    {
        BYTE bytes[16];
        memcpy(bytes,nops[i],lengths[i]);
        bytes[lengths[i]]=0xb8; bytes[lengths[i]+1]=42;
        memset(bytes+lengths[i]+2,0,3); bytes[lengths[i]+5]=0xc3;
        load_code(bytes,lengths[i]+6);
        expected_exception=0; skip=lengths[i];
        uint64_t result=call(0);
        int ok=result==42 && seen==0;
        printf("%s NOP form %u\n",ok?"PASS":"FAIL",i+1); failures+=!ok;
    }
    /* xgetbv; shl rdx,32; or rax,rdx; ret. ECX is the Windows argument. */
    const BYTE xgetbv[]={0x0f,0x01,0xd0,0x48,0xc1,0xe2,0x20,0x48,0x09,0xd0,0xc3};
    load_code(xgetbv,sizeof xgetbv); expected_exception=0; skip=3;
    uint64_t xcr0=call(0);
    int ok=seen==0 && (xcr0&3)==3;
    printf("%s XGETBV ecx=0, XCR0=0x%llx (not an AVX-512 claim)\n",
           ok?"PASS":"FAIL",(unsigned long long)xcr0); failures+=!ok;
    const BYTE cr3[]={0x48,0x0f,0x20,0xd8,0xc3}; /* mov rax,cr3; ret */
    load_code(cr3,sizeof cr3); expected_exception=EXCEPTION_PRIV_INSTRUCTION; skip=4;
    call(0); ok=seen==1 && unexpected==0;
    printf("%s privileged instruction maps to c0000096\n",ok?"PASS":"FAIL"); failures+=!ok;
    const BYTE ud2[]={0x0f,0x0b,0xc3};
    load_code(ud2,sizeof ud2); expected_exception=EXCEPTION_ILLEGAL_INSTRUCTION; skip=2;
    call(0); ok=seen==1 && unexpected==0;
    printf("%s UD2 remains c000001d\n",ok?"PASS":"FAIL"); failures+=!ok;
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(code,0,MEM_RELEASE);
    printf("Runtime checks: 6, failures: %d\n",failures);
    return failures?1:0;
}
