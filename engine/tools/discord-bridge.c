/* Windows helper run inside the Gryphfield prefix: the Windows end of Discord Rich Presence.
 * Games find the Discord client at the named pipe \\.\pipe\discord-ipc-0 (Endfield's
 * discord_partner_sdk.dll does). This program owns that pipe and relays each connection, byte for
 * byte, to gryphfield-discord (a native macOS helper) on 127.0.0.1:<port>, which relays it to
 * Discord's Unix socket. Wine's Winsock has no AF_UNIX, hence the loopback hop. The first line
 * sent on each TCP connection is the token from GRYPHFIELD_DISCORD_TOKEN, so other local users
 * cannot use the port. Design after xbridge (github.com/PlusoneChiang/xbridge), written anew.
 *
 *   discord-bridge.exe <port>          runs until killed
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -municode discord-bridge.c -o discord-bridge.exe -lws2_32 */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define PIPE_NAME L"\\\\.\\pipe\\discord-ipc-0"
#define BUF_SIZE 65536

static void log_line(const char *msg, DWORD err)
{
    fprintf(stderr, "discord-bridge: %s (%lu)\n", msg, err);
    fflush(stderr);
}

static SOCKET connect_helper(unsigned short port, const char *token)
{
    struct sockaddr_in addr = {0};
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int len = (int)strlen(token);

    if (sock == INVALID_SOCKET) return sock;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) || send(sock, token, len, 0) != len ||
        send(sock, "\n", 1, 0) != 1)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

/* pipe writes are small and the client reads them promptly; wait for each to finish */
static BOOL write_pipe(HANDLE pipe, OVERLAPPED *ov, const char *data, DWORD size)
{
    DWORD done;

    while (size)
    {
        ResetEvent(ov->hEvent);
        if (!WriteFile(pipe, data, size, NULL, ov) && GetLastError() != ERROR_IO_PENDING) return FALSE;
        if (!GetOverlappedResult(pipe, ov, &done, TRUE) || !done) return FALSE;
        data += done;
        size -= done;
    }
    return TRUE;
}

/* the socket is non-blocking after WSAEventSelect */
static BOOL send_all(SOCKET sock, const char *data, int size)
{
    fd_set set;
    int n;

    while (size > 0)
    {
        n = send(sock, data, size, 0);
        if (n == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK) return FALSE;
            FD_ZERO(&set);
            FD_SET(sock, &set);
            select(0, NULL, &set, NULL, NULL);
            continue;
        }
        data += n;
        size -= n;
    }
    return TRUE;
}

/* relay one connected client until either side closes */
static void relay(HANDLE pipe, SOCKET sock)
{
    static char from_pipe[BUF_SIZE], from_sock[BUF_SIZE];
    OVERLAPPED rd = {0}, wr = {0};
    WSAEVENT sev = WSACreateEvent();
    WSANETWORKEVENTS events;
    HANDLE waits[2];
    DWORD got;
    BOOL closing = FALSE;
    int n;

    rd.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    wr.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    WSAEventSelect(sock, sev, FD_READ | FD_CLOSE);
    if (!ReadFile(pipe, from_pipe, sizeof(from_pipe), NULL, &rd) && GetLastError() != ERROR_IO_PENDING)
        goto done;
    waits[0] = rd.hEvent;
    waits[1] = sev;

    while (!closing)
    {
        switch (WaitForMultipleObjects(2, waits, FALSE, INFINITE))
        {
        case WAIT_OBJECT_0:   /* the game sent something, or closed the pipe */
            if (!GetOverlappedResult(pipe, &rd, &got, FALSE) || !got) goto done;
            if (!send_all(sock, from_pipe, (int)got)) goto done;
            ResetEvent(rd.hEvent);
            if (!ReadFile(pipe, from_pipe, sizeof(from_pipe), NULL, &rd) && GetLastError() != ERROR_IO_PENDING)
                goto done;
            break;
        case WAIT_OBJECT_0 + 1:   /* Discord sent something, or the helper closed */
            if (WSAEnumNetworkEvents(sock, sev, &events)) goto done;
            if (events.lNetworkEvents & FD_CLOSE) closing = TRUE;
            while ((n = recv(sock, from_sock, sizeof(from_sock), 0)) > 0)
                if (!write_pipe(pipe, &wr, from_sock, (DWORD)n)) goto done;
            if (n == 0) closing = TRUE;
            else if (WSAGetLastError() != WSAEWOULDBLOCK) goto done;
            break;
        default:
            goto done;
        }
    }

done:
    CancelIo(pipe);
    GetOverlappedResult(pipe, &rd, &got, TRUE);
    CloseHandle(rd.hEvent);
    CloseHandle(wr.hEvent);
    WSACloseEvent(sev);
}

int wmain(int argc, wchar_t **argv)
{
    const char *token = getenv("GRYPHFIELD_DISCORD_TOKEN");
    unsigned short port = argc > 1 ? (unsigned short)_wtoi(argv[1]) : 0;
    OVERLAPPED ov = {0};
    WSADATA wsa;
    HANDLE pipe;
    SOCKET sock;
    DWORD got;

    if (!port || !token || !*token)
    {
        fprintf(stderr, "usage: GRYPHFIELD_DISCORD_TOKEN=... discord-bridge.exe <port>\n");
        return 2;
    }
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    pipe = CreateNamedPipeW(PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, BUF_SIZE, BUF_SIZE, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        log_line("cannot create \\\\.\\pipe\\discord-ipc-0", GetLastError());
        return 1;
    }
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    fprintf(stderr, "discord-bridge: listening on \\\\.\\pipe\\discord-ipc-0, helper port %u\n", port);
    fflush(stderr);

    for (;;)
    {
        ResetEvent(ov.hEvent);
        if (!ConnectNamedPipe(pipe, &ov))
        {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) GetOverlappedResult(pipe, &ov, &got, TRUE);
            else if (err != ERROR_PIPE_CONNECTED)
            {
                log_line("ConnectNamedPipe failed", err);
                Sleep(1000);
                continue;
            }
        }
        if ((sock = connect_helper(port, token)) == INVALID_SOCKET)
            log_line("the native helper is not reachable", WSAGetLastError());
        else
        {
            log_line("client connected", 0);
            relay(pipe, sock);
            closesocket(sock);
            log_line("client disconnected", 0);
        }
        DisconnectNamedPipe(pipe);
    }
}
