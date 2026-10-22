/* Harmless CPU/sleep workload for Wine + macOS sample integration tests. No game/ACE. */
#include <windows.h>
#include <stdio.h>
int main(void) {
    volatile unsigned long work = 1;
    puts("profile fixture ready");
    fflush(stdout);
    for (;;) {
        unsigned long i;
        for (i = 0; i < 100000; ++i) work = work * 1664525UL + 1013904223UL;
        Sleep(10);
    }
    return 0;
}
