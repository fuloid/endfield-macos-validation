/* Exercise the real x86_64 Wine/Rosetta signal path without loading the game.
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror rosetta_probe.c -o rosetta_probe.exe
 *
 * Every counted test runs on a freshly allocated page prepared the documented Windows way
 * (write as PAGE_READWRITE, switch to PAGE_EXECUTE_READ, FlushInstructionCache), so a stale
 * Rosetta translation of an earlier test cannot mask a result. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cpuid.h>

#define BAD_RESULT 0xdeadULL

typedef uint64_t (*call_fn)(unsigned);
static BYTE *code, *ret_stub;
static DWORD expected_exception;
static unsigned skip, seen, unexpected;

static LONG CALLBACK handle_exception(EXCEPTION_POINTERS *ep)
{
    if (!code || (uintptr_t)ep->ExceptionRecord->ExceptionAddress < (uintptr_t)code ||
        (uintptr_t)ep->ExceptionRecord->ExceptionAddress >= (uintptr_t)code + 4096)
        return EXCEPTION_CONTINUE_SEARCH;
    seen++;
    if (!expected_exception || ep->ExceptionRecord->ExceptionCode != expected_exception ||
        (uintptr_t)ep->ExceptionRecord->ExceptionAddress != (uintptr_t)code)
    {
        printf("  unexpected exception %08lx at +%llx\n", ep->ExceptionRecord->ExceptionCode,
               (unsigned long long)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)code));
        unexpected++;
        /* never resume at the faulting instruction: return from the test function instead */
        ep->ContextRecord->Rip = (uintptr_t)ret_stub;
        ep->ContextRecord->Rax = BAD_RESULT;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    ep->ContextRecord->Rip += skip;
    ep->ContextRecord->Rax = 0;
    ep->ContextRecord->Rdx = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static BOOL write_page(BYTE *page, const BYTE *bytes, unsigned len)
{
    DWORD old;
    if (!VirtualProtect(page, 4096, PAGE_READWRITE, &old)) return FALSE;
    memcpy(page, bytes, len);
    if (!VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old)) return FALSE;
    return FlushInstructionCache(GetCurrentProcess(), page, len);
}

/* fresh page per test */
static call_fn load_code(const BYTE *bytes, unsigned len)
{
    if (code) VirtualFree(code, 0, MEM_RELEASE);
    code = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!code || !write_page(code, bytes, len)) { printf("FAIL allocating test page\n"); ExitProcess(2); }
    seen = unexpected = 0;
    return (call_fn)(uintptr_t)code;
}

int main(void)
{
    int failures = 0, ok;
    unsigned a, b, c, d;
    PVOID handler;
    call_fn call;

    if (__get_cpuid(1, &a, &b, &c, &d))
        printf("CPUID: OSXSAVE=%u AVX=%u", (c >> 27) & 1, (c >> 28) & 1);
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d))
        printf(" AVX2=%u\n", (b >> 5) & 1);

    ret_stub = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ret_stub) return 2;
    ret_stub[0] = 0xc3;
    if (!VirtualProtect(ret_stub, 4096, PAGE_EXECUTE_READ, (DWORD *)&a)) return 2;
    handler = AddVectoredExceptionHandler(1, handle_exception);
    if (!handler) return 2;

    {
        const BYTE nops[][10] = {
            {0x0f,0x1f,0xc1}, {0x0f,0x1f,0x00},
            {0x66,0x2e,0x0f,0x1f,0x84,0,0,0,0,0},
            {0xf3,0x0f,0x1e,0xfa}, /* endbr64 */
            {0x0f,0x1f,0xc9}       /* 0F 1F /1 hint NOP */
        };
        const unsigned lengths[] = {3,3,10,4,3};
        for (unsigned i = 0; i < 5; i++)
        {
            BYTE bytes[16];
            memcpy(bytes, nops[i], lengths[i]);
            bytes[lengths[i]] = 0xb8; bytes[lengths[i] + 1] = 42;   /* mov eax,42; ret */
            memset(bytes + lengths[i] + 2, 0, 3); bytes[lengths[i] + 5] = 0xc3;
            call = load_code(bytes, lengths[i] + 6);
            expected_exception = 0; skip = 0;
            uint64_t result = call(0);
            ok = result == 42 && seen == 0;
            printf("%s NOP form %u\n", ok ? "PASS" : "FAIL", i + 1); failures += !ok;
        }
    }

    /* Informational: self-modifying code with the documented pattern (rewrite through
       VirtualProtect + FlushInstructionCache). Under Rosetta a freed and reused code address
       can keep a stale translation; CrossOver/FineWine share this, so it is not counted. */
    {
        const BYTE first[] = {0xb8,7,0,0,0,0xc3}, second[] = {0xb8,9,0,0,0,0xc3}; /* mov eax,N; ret */
        uint64_t r1, r2;
        call = load_code(first, sizeof first); expected_exception = 0; skip = 0;
        r1 = call(0);
        r2 = write_page(code, second, sizeof second) ? call(0) : BAD_RESULT;
        printf("INFO self-modifying code via VirtualProtect: got %llu then %llu (%s)\n",
               (unsigned long long)r1, (unsigned long long)r2,
               r1 == 7 && r2 == 9 ? "new code seen" : "stale translation");

        /* Informational only: rewrite an already executable RWX page with no protection
           change. Rosetta cannot see such writes; FineWine/CrossOver behave the same. */
        BYTE *rwx = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (rwx)
        {
            if (code) VirtualFree(code, 0, MEM_RELEASE);
            code = rwx; seen = unexpected = 0;
            memcpy(rwx, first, sizeof first);
            FlushInstructionCache(GetCurrentProcess(), rwx, sizeof first);
            r1 = ((call_fn)(uintptr_t)rwx)(0);
            memcpy(rwx, second, sizeof second);
            FlushInstructionCache(GetCurrentProcess(), rwx, sizeof second);
            r2 = ((call_fn)(uintptr_t)rwx)(0);
            printf("INFO raw RWX rewrite without VirtualProtect: got %llu then %llu (%s)\n",
                   (unsigned long long)r1, (unsigned long long)r2,
                   r2 == 9 ? "new code seen" : "stale translation");
        }
    }

    /* xgetbv; shl rdx,32; or rax,rdx; ret. ECX is the Windows argument. */
    {
        const BYTE xgetbv[] = {0x0f,0x01,0xd0,0x48,0xc1,0xe2,0x20,0x48,0x09,0xd0,0xc3};
        call = load_code(xgetbv, sizeof xgetbv); expected_exception = 0; skip = 0;
        uint64_t xcr0 = call(0);
        ok = seen == 0 && (xcr0 & 3) == 3 && xcr0 != BAD_RESULT;
        printf("%s XGETBV ecx=0, XCR0=0x%llx (not an AVX-512 claim)\n",
               ok ? "PASS" : "FAIL", (unsigned long long)xcr0); failures += !ok;
    }
    {
        const BYTE cr3[] = {0x48,0x0f,0x20,0xd8,0xc3}; /* mov rax,cr3; ret */
        call = load_code(cr3, sizeof cr3); expected_exception = EXCEPTION_PRIV_INSTRUCTION; skip = 4;
        call(0); ok = seen == 1 && unexpected == 0;
        printf("%s privileged instruction maps to c0000096\n", ok ? "PASS" : "FAIL"); failures += !ok;
    }
    {
        const BYTE ud2[] = {0x0f,0x0b,0xc3};
        call = load_code(ud2, sizeof ud2); expected_exception = EXCEPTION_ILLEGAL_INSTRUCTION; skip = 2;
        call(0); ok = seen == 1 && unexpected == 0;
        printf("%s UD2 remains c000001d\n", ok ? "PASS" : "FAIL"); failures += !ok;
    }

    RemoveVectoredExceptionHandler(handler);
    if (code) VirtualFree(code, 0, MEM_RELEASE);
    VirtualFree(ret_stub, 0, MEM_RELEASE);
    printf("Runtime checks: 8, failures: %d\n", failures);
    return failures ? 1 : 0;
}
