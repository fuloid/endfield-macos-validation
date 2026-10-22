/* Checks Gryphfield's Discord bridge from the game's side: connects to \\.\pipe\discord-ipc-0 as a
 * game's Discord SDK does, sends a HANDSHAKE (client id 1234567890) and a SET_ACTIVITY, and prints
 * Discord's replies. engine/ci-smoke.sh runs it through `gryphfield-engine _discord-selftest`
 * against a fake Discord socket. Exit code 0 when both replies arrive.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HANDLE pipe;

static BOOL send_frame(DWORD op, const char *json)
{
    DWORD header[2] = {op, (DWORD)strlen(json)}, done;

    return WriteFile(pipe, header, sizeof(header), &done, NULL) && done == sizeof(header) &&
           WriteFile(pipe, json, header[1], &done, NULL) && done == header[1];
}

static BOOL read_exact(void *buf, DWORD size)
{
    DWORD done;

    while (size)
    {
        if (!ReadFile(pipe, buf, size, &done, NULL) || !done) return FALSE;
        buf = (char *)buf + done;
        size -= done;
    }
    return TRUE;
}

static BOOL read_frame(const char *what)
{
    static char payload[65536];
    DWORD header[2];

    if (!read_exact(header, sizeof(header)) || header[1] >= sizeof(payload) || !read_exact(payload, header[1]))
    {
        printf("no %s reply (%lu)\n", what, GetLastError());
        return FALSE;
    }
    payload[header[1]] = 0;
    printf("%s reply op=%lu %s\n", what, header[0], payload);
    return TRUE;
}

int main(void)
{
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < 100; i++)
    {
        pipe = CreateFileW(L"\\\\.\\pipe\\discord-ipc-0", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    }
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf("cannot open \\\\.\\pipe\\discord-ipc-0 (%lu)\n", GetLastError());
        return 1;
    }
    if (!send_frame(0, "{\"v\":1,\"client_id\":\"1234567890\"}") || !read_frame("handshake")) return 1;
    if (!send_frame(1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4242,\"activity\":{\"state\":\"pipe probe\"}},\"nonce\":\"probe-1\"}") ||
        !read_frame("set_activity"))
        return 1;
    CloseHandle(pipe);
    printf("discord pipe probe passed\n");
    return 0;
}
